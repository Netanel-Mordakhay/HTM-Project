import os
import multiprocessing as mp
from itertools import chain

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from htm_source.model.htm_pyramid import ModelPyramid
from htm_source.utils.fs import load_config
from htm_source.utils.general import smooth_predictions
from htm_source.utils.metric import merge_targets_and_predictions, plot_windows, find_best_score
from htm_source.utils.graph import build_and_validate_graph, get_layer_dict
import logging

if __name__ == '__main__':
    mp.set_start_method('spawn')
    # Load config & data
    _repo_root = os.path.dirname(os.path.abspath(__file__))
    config_path_data = os.path.join(_repo_root, 'config', 'data', 'config--swat.yaml')
    config_path_model = os.path.join(_repo_root, 'config', 'model', 'config--model_default.yaml')
    # data_path = "/home/vlad/Theses/SCADA-data/SWaT.A1_A2_Dec_2015/Physical/SWaT_Dataset_Full_v0_Cleaned.parquet"
    data_path = os.path.join(_repo_root, 'data', 'swat_dataset.parquet')
    data_cfg = load_config(config_path_data)
    run_cfg = load_config(config_path_model)

    # if not os.path.exists(data_path):
    #     data_path = "/vlad/SWaT_Dataset_Full_v0_Cleaned.parquet"

    full_data = pd.read_parquet(data_path)

    general_cfg = run_cfg['general']

    grace_window = general_cfg['grace_window']
    learn_period = general_cfg['learn_period']
    thresh = np.arange(*general_cfg['thresholds'])
    threshold_delta = general_cfg['threshold_delta']
    smooth_box_sizes = np.arange(*general_cfg['smooth_box_sizes'])
    min_data = general_cfg['data_min']
    max_data = general_cfg['data_max']
    res_data = general_cfg['data_res']

    features = {
        'L0_1': ('mv101', 'fit101', 'lit101'),  # stage 1
        'L0_2': ('lit101', 'fit201', 'p101'),

        'L0_3': ('ait201', 'p201'),
        'L0_4': ('ait202', 'p203', 'ait402'),
        'L0_5': ('ait203', 'p205', 'ait402'),

        'L0_6': ('lit301', 'fit201', 'p101',),  # stage 2-3
        'L0_7': ('dpit301', 'p302', 'fit301'),
        'L0_8': ('lit301', 'fit301', 'p302'),

        'L0_9': ('lit401', 'fit301', 'p302'),  # stage 3-4
        'L0_10': ('fit401', 'p402', 'uv401'),
        'L0_11': ('ait401', 'ait402', 'p403'),

        'L0_12': ('fit501', 'pit501', 'p501'),
        'L0_13': ('fit502', 'pit502', 'ait504'),
        'L0_14': ('ait501', 'ait502', 'ait503'),

        'L0_15': ('fit503', 'pit503', 'fit504'),  # stage 5 --> 5
        'L0_16': ('fit601', 'p602', 'dpit301')  # stage 6 --> 3
    }

    connections = {
            'L1_1': ('L0_1', 'L0_2'),
            'L1_2': ('L0_3', 'L0_4', 'L0_5'),
            'L1_3': ('L0_6', 'L0_7', 'L0_8'),
            'L1_4': ('L0_9', 'L0_10', 'L0_11'),
            'L1_5': ('L0_12', 'L0_13', 'L0_14'),
            'L1_6': ('L0_15', 'L0_16'),

            'L2_1': ('L1_1', 'L1_2'),
            'L2_2': ('L1_3', 'L1_4'),
            'L2_3': ('L1_5', 'L1_6'),

            'L3_1': ('L2_1', 'L2_2', 'L2_3')
    }

    data = full_data.copy().iloc[min_data:max_data:res_data].reset_index(drop=True)
    X = data[[c for c in data.columns if c in set(chain(*features.values()))]]
    y = data['label'].values

    graph = build_and_validate_graph(features, connections)
    layer_dict = get_layer_dict(graph)

    model = ModelPyramid(data=X,
                         feat_cfg=data_cfg['features'],
                         enc_cfg=run_cfg['encoders'],
                         sp_cfg=run_cfg['models']['sp'],
                         tm_cfg=run_cfg['models']['tm'],
                         seed=general_cfg['seed'],
                         feature_plan=features,
                         network_graph=graph,
                         layer_dict=layer_dict,
                         feature_merge_mode=general_cfg['feature_merge_mode'],
                         anomaly_score=True,
                         max_pool=general_cfg['max_pool'],
                         htm_merge_mode=general_cfg['htm_merge_mode'],
                         use_predictive=general_cfg['use_predictive'],
                         lazy_init_htm=True)

    model.model_summary()
    model.run()

    all_scores = model.get_scores(df=True)

    # np.save('bad-score', score)

    preds = all_scores['_head_'].values
    # preds = np.load('bad-score.npy')

    final_df = pd.concat((X, all_scores), axis=1)
    final_df['label'] = y

    htm_score, metrics, params = find_best_score(preds, y,
                                                 thresholds=thresh,
                                                 smoothing_window_sizes=smooth_box_sizes,
                                                 learn_period=learn_period,
                                                 thresh_delta=threshold_delta,
                                                 grace_window=grace_window,
                                                 optimize='F1.0')
    t = params['thresh']
    b = params['box_size']
    final_df['smooth_preds'] = smooth_predictions(preds, b)

    if htm_score > 0:
        pred_merged, target_merged = merge_targets_and_predictions(preds, y, thresh=t, smooth_box_size=b,
                                                                   learn_period=learn_period, grace_window=grace_window)
        plot_windows(pred_merged, target_merged, preds, y, t, learn_period)
    else:
        print("The model didn't learn, best score is 0!")

    model.plot_results(y, t)

    # plt.figure(figsize=(15, 5))
    # plt.plot(n_score, label='score')
    # # plt.plot(pred, label='alikl')
    # plt.plot(y, label='label')
    # plt.legend()
    # plt.show()
    #
    # plt.figure(figsize=(15, 5))
    # plt.plot(n_score[-1500:], label='score')
    # # plt.plot(pred, label='alikl')
    # plt.plot(y[-1500:], label='label')
    # plt.legend()
    # plt.show()
    # print(1)
