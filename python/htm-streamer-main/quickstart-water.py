import os
import multiprocessing as mp
from itertools import chain

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from htm_source.model.htm_pyramid import ModelPyramid
from htm_source.utils.fs import load_config
from htm_source.utils.metric import find_best_fb, merge_targets_and_predictions, plot_windows
import logging

if __name__ == '__main__':
    mp.set_start_method('spawn')
    # Load config & data
    config_path_user = os.path.join(os.getcwd(), 'config', 'config--datagen-water.yaml')
    config_path_model = os.path.join(os.getcwd(), 'config', 'config--model_default.yaml')
    data_path = "/home/vlad/Theses/datagen/sim_water.csv"
    data_cfg = load_config(config_path_user)
    model_cfg = load_config(config_path_model)
    full_data = pd.read_csv(data_path)

    res_data = 1
    min_data = 0
    max_data = 10_000
    learn_period = 2_000

    plan = [('level', 'f_in', 'f_out', )]

    merge_mode = 'sd'
    layer_inputs = 4
    max_pool = 1

    data = full_data.copy().iloc[min_data:max_data:res_data].reset_index(drop=True)
    data['label'] = 0
    data[['f_in', 'f_out']] = data[['f_in', 'f_out']].astype(int)
    X = data[[c for c in data.columns if c in set(chain(*plan))]]

    model = ModelPyramid(data=X,
                         inputs_per_layer=layer_inputs,
                         feat_cfg=data_cfg['features'],
                         enc_cfg=model_cfg['models_encoders'],
                         sp_cfg=model_cfg['models_params']['sp'],
                         tm_cfg=model_cfg['models_params']['tm'],
                         seed=model_cfg['models_params']['seed'],
                         feature_mode='plan',
                         feature_plan=plan,
                         anomaly_score=True,
                         al_learn_period=learn_period,
                         max_pool=max_pool,
                         flatten=True,
                         merge_mode=merge_mode,
                         feature_join_str='+')

    model.model_summary()
    model.run()

    pred = np.array(model.head.anomaly['likelihood'])

    # pred = pd.read_csv('eye-state-results-nab.csv')['anomaly_score'].values

    gt = data['label'].values

    # model.plot_results(gt, 0.85)
    Xd = X[[c for c in X.columns if c != 'timestamp']]
    plt.figure(figsize=(15, 5))
    plt.plot((Xd - Xd.min()) / (Xd.max() - Xd.min()))
    plt.legend()
    plt.show()
    # print(pred[learn_period:].mean())

    thresh = np.arange(0., 1., 0.01)
    winds = np.arange(5, 55, 5)

    score, (t, w) = find_best_fb(pred, gt, thresh, winds, learn_period=learn_period, beta=1, pred_window_size=2)
    if score > 0:
        pred_merged, target_merged = merge_targets_and_predictions(pred, gt, t, w, learn_period)
        plot_windows(pred_merged, target_merged, pred, gt, t, learn_period)
    else:
        print("The model didn't learn, best score is 0!")

    model.plot_results(gt, t if t else 0.9)

    print(1)
