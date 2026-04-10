import os
import multiprocessing as mp
import time
from itertools import chain
from functools import partial

import numpy as np
import pandas as pd
import networkx as nx
import shutil
import matplotlib.pyplot as plt

from htm_source.model.htm_pyramid import ModelPyramid
from htm_source.utils.fs import load_config
from htm_source.utils.general import smooth_predictions
from htm_source.utils.graph import get_layer_dict, build_and_validate_graph
from htm_source.utils.metric import find_best_score, merge_targets_and_predictions, plot_windows
import logging
import optuna


def main():
    mp.set_start_method('spawn')
    # Load config & data
    config_path_data = os.path.join(os.getcwd(), 'config', 'data', 'config--swat.yaml')
    study_name = "htm-study-47F-GA-3"  # Unique identifier of the study.
    storage_name = f"sqlite:////home/vlad/Downloads/{study_name}.db"
    trial_idx = 5412
    data_path = "/vlad/SWaT_Dataset_Full_v0_Cleaned.parquet"

    if not os.path.exists(data_path):
        data_path = "/home/vlad/Theses/SCADA-data/SWaT.A1_A2_Dec_2015/Physical/SWaT_Dataset_Full_v0_Cleaned.parquet"

    data_cfg = load_config(config_path_data)
    full_data = pd.read_parquet(data_path)

    study = optuna.create_study(study_name=study_name, storage=storage_name, direction='maximize', load_if_exists=True)
    run_cfg, feature_plan, connections = load_trial_config(study, trial_idx)
    trial = study.trials[trial_idx]
    print(f"Loaded Trial #{trial_idx} from study '{study_name}'")
    print(f"Expecting metric: {trial.value:.3f}\t"
          # f"F1: {trial.user_attrs['F1.0']:.3f}\t"
          f"HoU: {trial.user_attrs['hou']:.3f}\t"
          f"IoL: {trial.user_attrs['iol']:.3f}")

    general_cfg = run_cfg['general']

    grace_window = int(general_cfg['grace_window'])
    learn_period = int(general_cfg['learn_period'])
    thresh = np.arange(*general_cfg['thresholds'])
    threshold_delta = general_cfg['threshold_delta']
    smooth_box_sizes = np.arange(*general_cfg['smooth_box_sizes'])
    min_data = int(general_cfg['data_min'])
    max_data = int(general_cfg['data_max'])
    res_data = int(general_cfg['data_res'])

    data = full_data.copy().iloc[min_data:max_data:res_data].reset_index(drop=True)
    X = data[[c for c in data.columns if c in set(chain(*feature_plan.values()))]]
    y = data['label'].values

    graph = build_and_validate_graph(feature_plan, connections)
    layer_dict = get_layer_dict(graph)

    save_file_name = f"final_df__{study_name}__{trial_idx}__[{min_data}:{max_data}:{res_data}].csv"

    if os.path.isfile(save_file_name) and os.path.getsize(save_file_name) > 0:
        final_df = pd.read_csv(save_file_name)
        preds = final_df['_head_']

    else:
        model = ModelPyramid(data=X,
                             feat_cfg=data_cfg['features'],
                             enc_cfg=run_cfg['encoders'],
                             sp_cfg=run_cfg['models']['sp'],
                             tm_cfg=run_cfg['models']['tm'],
                             seed=int(general_cfg['seed']),
                             feature_plan=feature_plan,
                             network_graph=graph,
                             layer_dict=layer_dict,
                             feature_merge_mode=general_cfg['feature_merge_mode'],
                             anomaly_score=True,
                             prepare_encodings=True,
                             lazy_init_htm=True,
                             max_pool=general_cfg['max_pool'],
                             htm_merge_mode=general_cfg['htm_merge_mode'], )
        model.model_summary()
        model.run()
        all_scores = model.get_scores(df=True)
        preds = all_scores['_head_'].values
        final_df = pd.concat((X, all_scores), axis=1)
        final_df['label'] = y
        final_df.to_csv(save_file_name)

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
    print(f'{b=}, {t=}')
    if htm_score > 0:
        pred_merged, target_merged = merge_targets_and_predictions(preds, y, thresh=t, smooth_box_size=b,
                                                                   learn_period=learn_period, grace_window=grace_window)
        plot_windows(pred_merged, target_merged, preds, y, t, learn_period)
    else:
        print("The model didn't learn, best score is 0!")

    model.plot_results(y, t)


def load_trial_config(study: optuna.Study, trial_idx: int) -> tuple[dict, dict, dict]:
    config = load_config(os.path.join(os.getcwd(), 'config', 'model', 'config--model_empty.yaml'))
    trial: dict = study.trials_dataframe().iloc[trial_idx].to_dict().copy()

    feature_plan = {key: tuple(val) for key, val in trial['user_attrs_feature_plan'].items()}
    try:
        connections = {key: tuple(val) for key, val in trial['user_attrs_connections'].items()}
    except KeyError:
        connections = {  # TODO temp
            'L1_1': ('L0_1', 'L0_2'),
            'L1_2': ('L0_3', 'L0_4', 'L0_5'),
            'L1_3': ('L0_6', 'L0_7', 'L0_8'),
            'L1_4': ('L0_9', 'L0_10', 'L0_11'),
            'L1_5': ('L0_12', 'L0_13', 'L0_14'),
            'L1_6': ('L0_15', 'L0_16'),

            'L2_1': ('L1_1', 'L1_2'),
            'L2_2': ('L1_3', 'L1_4'),
            'L2_3': ('L1_5', 'L1_6'),

            'L3_1': ('L2_1', 'L2_2', 'L2_3')}

    graph = build_and_validate_graph(feature_plan, connections)
    n_layers = len(nx.dag_longest_path(graph))

    layered_params = {}

    for trial_key, trial_value in trial.items():
        key_parts = trial_key.split('_')

        # irrelevant bits for now
        if len(key_parts) < 3:
            continue

        # optuna-found config
        if key_parts[0] == 'params' and key_parts[1] in {'tm', 'sp'}:
            _, typ, name, *layer_code = key_parts
            if trial_value % 1 == 0.0:
                trial_value = int(trial_value)

            if not layer_code:
                config["models"][typ][name] = trial_value
            else:
                layer_code = layer_code.pop()  # 1 element list due to *
                if not layer_code[0] == 'L':
                    raise ValueError(f"Error reading layer code: {typ, name, layer_code}")

                layer_key = f'{typ}_{name}'
                l_params = layered_params.get(layer_key, [None] * n_layers)
                l_params[int(layer_code[1:])] = trial_value
                layered_params[layer_key] = l_params

        # user defined config
        elif key_parts[0] == 'user' and key_parts[1] == 'attrs':
            param_name = '_'.join(key_parts[2:])
            if param_name in config['general']:
                config['general'][param_name] = trial_value

    # tuplify layer-dependant params
    for key, value in layered_params.items():
        t, k = key.split('_')
        config["models"][t][k] = tuple(value)

    # handle special cases
    config['encoders']['n'] = trial['params_encoder_n']
    config['encoders']['w'] = trial['params_encoder_w']

    try:
        config['general']['max_pool'] = tuple([trial[f'params_max_pool_L{i}'] for i in range(n_layers)])
    except KeyError:
        config['general']['max_pool'] = trial['params_max_pool']

    return config, feature_plan, connections


if __name__ == '__main__':
    main()
