import os
import multiprocessing as mp
from itertools import chain

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from htm_source.model.htm_pyramid import ModelPyramid
from htm_source.utils.fs import load_config
from htm_source.utils.general import smooth_predictions
from htm_source.utils.graph import build_and_validate_graph, get_layer_dict
from htm_source.utils.metric import find_best_fb, merge_targets_and_predictions, plot_windows, find_best_htm_score
import logging

if __name__ == '__main__':
    mp.set_start_method('spawn')
    # Load config & data
    config_path_user = os.path.join(os.getcwd(), 'config', 'data', 'config--eyestate.yaml')
    config_path_model = os.path.join(os.getcwd(), 'config', 'model', 'config--model_default.yaml')
    data_path = "/home/vlad/Theses/eye-state/eye-state.csv"
    data_cfg = load_config(config_path_user)
    run_cfg = load_config(config_path_model)

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
        'L0_1': ('AF3', 'F7', 'F3', 'FC5'),
        'L0_2': ('T7', 'P7', 'O1'),
        'L0_3': ('O2', 'P8', 'T8', 'FC6'),
        'L0_4': ('F4', 'F8', 'AF4')
    }

    connections = {
        'L1_1': ('L0_1', 'L0_2'),
        'L1_2': ('L0_3', 'L0_4'),

        'L2_1': ('L1_1', 'L1_2')
    }

    data = pd.read_csv(data_path).loc[:]

    X = data[[c for c in data.columns if c in set(chain(*features.values()))]]
    y = data['eyeDetection'].values

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
    preds = all_scores['_head_'].values

    final_df = pd.concat((X, all_scores), axis=1)
    final_df['label'] = y

    htm_score, metrics, params = find_best_htm_score(preds, y, thresh, smooth_box_sizes,
                                                     learn_period=learn_period,
                                                     thresh_delta=threshold_delta,
                                                     grace_window=grace_window)
    t = params['thresh']
    b = params['box_size']
    final_df['smooth_preds'] = smooth_predictions(preds, b)

    if htm_score > 0:
        pred_merged, target_merged = merge_targets_and_predictions(preds, y, thresh=t, smooth_box_size=b,
                                                                   learn_period=learn_period, grace_window=grace_window)
        plot_windows(pred_merged, target_merged, preds, y, t, learn_period)
    else:
        print("The model didn't learn, best score is 0!")

    # model.plot_results(data['eyeDetection'])

    print(1)
