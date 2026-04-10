import os
import multiprocessing as mp
import time
from itertools import chain
from functools import partial

import numpy as np
import pandas as pd
import shutil
import matplotlib.pyplot as plt

from htm_source.model.htm_pyramid import ModelPyramid
from htm_source.utils.fs import load_config
from htm_source.utils.graph import build_and_validate_graph, get_layer_dict
from htm_source.utils.metric import find_best_score
import logging
import optuna


def optimize_htm(trial: optuna.Trial, full_data: pd.DataFrame, run_cfg: dict, data_cfg: dict) -> float:
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

    t0 = time.perf_counter()

    general_cfg = run_cfg['general']
    min_data = general_cfg['data_min']
    max_data = general_cfg['data_max']
    res_data = general_cfg['data_res']

    # select relevant data
    data = full_data.copy().iloc[min_data:max_data:res_data].reset_index(drop=True)
    X = data[[c for c in data.columns if c in set(chain(*features.values()))]]
    y = data['label'].values

    # get network config stuff
    graph = build_and_validate_graph(features, connections)
    layer_dict = get_layer_dict(graph)
    num_layers = len(layer_dict)

    encode_cfg = run_cfg['encoders']
    sp_cfg = run_cfg['models']['sp']
    tm_cfg = run_cfg['models']['tm']

    # post-process params
    grace_window = general_cfg['grace_window']
    learn_period = general_cfg['learn_period']
    thresh = np.arange(*general_cfg['thresholds'])
    threshold_delta = general_cfg['threshold_delta']
    smooth_box_sizes = np.arange(*general_cfg['smooth_box_sizes'])

    # hyper parameters
    max_pool = [trial.suggest_categorical(f'max_pool_L{i}', [1]) for i in range(num_layers)]

    # encoder parameters
    enc_n = trial.suggest_int('encoder_n', 512, 1024, step=64)
    enc_w = trial.suggest_float('encoder_w', 0.015, 0.04, step=0.005)

    # sp parameters
    # sp_wraparound = sp_cfg["wrapAround"]
    sp_col_dim = trial.suggest_int('sp_columnDimensions', 1024, 2048, step=128)
    sp_local_dense = trial.suggest_float('sp_localAreaDensity', 0.015, 0.04, step=0.005)
    sp_pot_pct = trial.suggest_float('sp_potentialPct', 0.1, 0.5, step=0.05)
    # sp_radius = trial.suggest_float('sp_potentialRadius', 0.25, 0.45 * (2 - int(sp_wraparound)), step=0.05)  # TODO: all 1
    # sp_global_inh = [trial.suggest_categorical(f'sp_globalInhibition_L{i}', [True, False]) for i in range(num_layers)]  # TODO: all True
    sp_boost = [trial.suggest_float(f'sp_boostStrength_L{i}', 0, 3, step=0.2) for i in range(num_layers)]
    sp_stim_thresh = [trial.suggest_int(f'sp_stimulusThreshold_L{i}', 4, 40, step=4) for i in range(num_layers)]  # TODO: per layer
    # sp_syn_perm_conn = trial.suggest_float('sp_synPermConnected', 0.25, 0.55, step=0.03)  # TODO 0.5 all
    sp_perm_inc = [trial.suggest_float(f'sp_synPermActiveInc_L{i}', 0.00005, 0.5, log=True) for i in range(num_layers)]
    sp_perm_dec = [trial.suggest_float(f'sp_synPermInactiveDec_L{i}', 0.000005, 0.1, log=True) for i in range(num_layers)]
    # sp_pct_overlap_duty = trial.suggest_float('sp_minPctOverlapDutyCycle', 0.001, 0.1, log=True)  # TODO all 0.001
    sp_duty_cyc_period = [trial.suggest_int(f'sp_dutyCyclePeriod_L{i}', 500, 2450, step=150) for i in range(num_layers)]         # TODO layer wise

    # tm parameters
    # tm_cells = [trial.suggest_int(f'tm_cellsPerColumn_L{i}', 2, 6) for i in range(num_layers)]  # TODO all 4
    # tm_min_thresh = trial.suggest_int('tm_minThreshold', 5, 20)  # TODO all 16
    tm_active_thresh = [trial.suggest_int(f'tm_activationThreshold_L{i}', 17, 80) for i in range(num_layers)]  # TODO layer wise
    tm_new_syn = [trial.suggest_int(f'tm_newSynapseCount_L{i}', 32, 64, step=4) for i in range(num_layers)]  # TODO all 48
    tm_max_syn_seg = [trial.suggest_int(f'tm_maxSynapsesPerSegment_L{i}', 192, 512, step=32) for i in range(num_layers)]  # TODO all 256
    tm_max_seg_cell = [trial.suggest_int(f'tm_maxSegmentsPerCell_L{i}', 64, 192, step=16) for i in range(num_layers)]  # TODO all 64
    # tm_init_perm = trial.suggest_float('tm_initialPerm', 0.3, 0.5, step=0.02)  # TODO all 0.21
    # tm_perm_conn = trial.suggest_float('tm_permanenceConnected', tm_init_perm + 0.05, 0.65, step=0.02) #  TODO all 0.6
    tm_perm_inc = [trial.suggest_float(f'tm_permanenceInc_L{i}', 0.025, 0.3, step=0.025) for i in range(num_layers)]  # TODO layer wise
    tm_perm_dec = [trial.suggest_float(f'tm_permanenceDec_L{i}', 0.000005, 0.5, log=True) for i in range(num_layers)]  # TODO layer wise
    tm_pred_seg_dec = [2 * tm_perm_inc[i] * sp_local_dense for i in range(num_layers)]

    # validity checks
    pass

    # inject back into cfg
    # general_cfg['htm_merge_mode'] = htm_merge_mode
    general_cfg['max_pool'] = tuple(max_pool)

    encode_cfg['n'] = enc_n
    encode_cfg['w'] = enc_w

    sp_cfg['potentialPct'] = sp_pot_pct
    sp_cfg['columnDimensions'] = sp_col_dim
    # sp_cfg['potentialRadius'] = sp_radius
    sp_cfg['localAreaDensity'] = sp_local_dense
    # sp_cfg['globalInhibition'] = tuple(sp_global_inh)
    sp_cfg['boostStrength'] = tuple(sp_boost)
    sp_cfg['stimulusThreshold'] = tuple(sp_stim_thresh)
    # sp_cfg['synPermConnected'] = sp_syn_perm_conn
    sp_cfg['synPermActiveInc'] = tuple(sp_perm_inc)
    sp_cfg['synPermInactiveDec'] = tuple(sp_perm_dec)
    # sp_cfg['wrapAround'] = sp_wraparound
    # sp_cfg['minPctOverlapDutyCycle'] = sp_pct_overlap_duty
    sp_cfg['dutyCyclePeriod'] = tuple(sp_duty_cyc_period)

    # tm_cfg['cellsPerColumn'] = tuple(tm_cells)
    # tm_cfg['minThreshold'] = tm_min_thresh
    tm_cfg['activationThreshold'] = tuple(tm_active_thresh)
    # tm_cfg['initialPerm'] = tm_init_perm
    tm_cfg['maxSegmentsPerCell'] = tuple(tm_max_seg_cell)
    tm_cfg['maxSynapsesPerSegment'] = tuple(tm_max_syn_seg)
    tm_cfg['newSynapseCount'] = tuple(tm_new_syn)
    # tm_cfg['permanenceConnected'] = tm_perm_conn
    tm_cfg['permanenceInc'] = tuple(tm_perm_inc)
    tm_cfg['permanenceDec'] = tuple(tm_perm_dec)
    tm_cfg['predictedSegmentDecrement'] = tuple(tm_pred_seg_dec)

    # Update DB
    try:
        shutil.copy(f"{trial.study.study_name}.db", "/vlad/volume")
    except FileNotFoundError:
        pass  # TODO add error?
    except Exception as e:
        raise e

    print(f"STARTING TRIAL #{trial.number}")
    model = ModelPyramid(data=X,
                         feat_cfg=data_cfg['features'].copy(),
                         enc_cfg=encode_cfg.copy(),
                         sp_cfg=sp_cfg.copy(),
                         tm_cfg=tm_cfg.copy(),
                         seed=general_cfg['seed'],
                         feature_plan=features.copy(),
                         network_graph=graph,
                         layer_dict=layer_dict.copy(),
                         feature_merge_mode=general_cfg['feature_merge_mode'],
                         anomaly_score=True,
                         max_pool=general_cfg['max_pool'],
                         flatten=True,
                         htm_merge_mode=general_cfg['htm_merge_mode'],
                         use_predictive=general_cfg['use_predictive'],
                         lazy_init_htm=True)

    # Manual log stuff
    trial.set_user_attr('seed', general_cfg['seed'])
    trial.set_user_attr('grace_window', general_cfg['grace_window'])
    trial.set_user_attr('learn_period', general_cfg['learn_period'])
    trial.set_user_attr('threshold_delta', general_cfg['threshold_delta'])
    trial.set_user_attr('feature_merge_mode', general_cfg['feature_merge_mode'])
    trial.set_user_attr('htm_merge_mode', general_cfg['htm_merge_mode'])
    trial.set_user_attr('sp_wrapAround', sp_cfg['wrapAround'])
    trial.set_user_attr('encoding_summary', model.ds.get_encoding_summary())
    trial.set_user_attr('feature_plan', features)
    trial.set_user_attr('connections', connections)
    trial.set_user_attr('data_min', general_cfg['data_min'])
    trial.set_user_attr('data_max', general_cfg['data_max'])
    trial.set_user_attr('data_res', general_cfg['data_res'])
    trial.set_user_attr('use_predictive', general_cfg['use_predictive'])

    model.model_summary()
    model.run()

    predictions = np.array(model.head.anomaly['score'])

    htm_score, metrics, params = find_best_score(predictions, y, thresh, smooth_box_sizes,
                                                 learn_period=learn_period,
                                                 thresh_delta=threshold_delta,
                                                 grace_window=grace_window,
                                                 optimize='F1.0')

    # log post process params
    [trial.set_user_attr(k, v) for k, v in params.items()]

    # log auxiliary metrics
    [trial.set_user_attr(k, v) for k, v in metrics.items()]

    trial.set_user_attr('duration', time.perf_counter() - t0)

    return htm_score


def main():
    # mp.set_start_method('spawn')
    # Load config & data
    config_path_data = os.path.join(os.getcwd(), 'config', 'data', 'config--swat.yaml')
    config_path_model = os.path.join(os.getcwd(), 'config', 'model', 'config--model_empty.yaml')
    data_path = "/vlad/SWaT_Dataset_Full_v0_Cleaned.parquet"

    if not os.path.exists(data_path):
        data_path = "/home/vlad/Theses/SCADA-data/SWaT.A1_A2_Dec_2015/Physical/SWaT_Dataset_Full_v0_Cleaned.parquet"

    data_cfg = load_config(config_path_data)
    run_cfg = load_config(config_path_model)
    full_data = pd.read_parquet(data_path)

    study_name = "htm-study-47F-GA-6"  # Unique identifier of the study.
    storage_name = f"sqlite:///{study_name}.db"
    part = partial(optimize_htm, full_data=full_data.copy(), run_cfg=run_cfg, data_cfg=data_cfg)
    study = optuna.create_study(study_name=study_name, storage=storage_name, direction='maximize', load_if_exists=True)
    study.optimize(part, gc_after_trial=True)


if __name__ == '__main__':
    main()