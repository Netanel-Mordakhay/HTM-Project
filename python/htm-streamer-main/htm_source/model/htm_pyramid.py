from __future__ import annotations

import time
from typing import Tuple, List, Dict

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import multiprocessing as mp
import networkx as nx

from htm.bindings.sdr import SDR
from tqdm import tqdm

from htm_source.data.data_streamer import DataStreamer
from htm_source.model.htm_model import HTMModule
from htm_source.utils.general import manual_seed, get_random_sig, \
    get_layer_config
from htm_source.utils.sdr import sdr_merge, concat_shapes, flatten_shape


class ModelPyramid:
    def __init__(self,
                 data: pd.DataFrame,
                 feat_cfg: dict,
                 enc_cfg: dict,
                 sp_cfg: dict | None,
                 tm_cfg: dict,
                 seed: int,
                 feature_plan: dict,
                 network_graph: nx.DiGraph,
                 layer_dict: dict,
                 train_percent: float = 1.0,
                 prepare_encodings: bool = False,
                 lazy_init_htm: bool = False,
                 feature_join_str: str = '_',
                 feature_merge_mode: str = 'u',
                 htm_merge_mode: str = 'u',
                 concat_axis: int = 0,
                 max_pool: int | list[int] = 1,
                 flatten: bool = True,
                 learn_schedule: int = None,
                 anomaly_score: bool = True,
                 multiprocess: bool = False,
                 use_predictive: bool = False):
        """
        TODO
        """

        manual_seed(seed)
        print(f"RANDOM {get_random_sig()}")

        self.graph = network_graph
        self._layer_dict = layer_dict

        # print(f"Features chosen: {feature_plan}")

        self._ds = DataStreamer(data,
                                features_cfg=feat_cfg.copy(),
                                encoders_cfg=enc_cfg.copy(),
                                train_percent=train_percent,
                                feature_merge_mode=feature_merge_mode,
                                merge_plan=feature_plan,
                                prepare=prepare_encodings,
                                seed=seed,
                                feature_join_str=feature_join_str)

        self.lazy_init = lazy_init_htm
        self.mp = multiprocess
        self.max_pool = max_pool
        self.flat = flatten
        self.calc_anomaly = anomaly_score
        self.lr_sch = learn_schedule
        self.merge_mode = htm_merge_mode
        self.concat_axis = concat_axis
        self.use_predictive = use_predictive

        self._fjs = feature_join_str
        self._configuration = {'sp': sp_cfg, 'tm': tm_cfg}
        self._seed = seed
        self._head = None
        self._n_models = len(self.graph)
        self._n_layers = len(self._layer_dict)
        self._current_iter = 0
        self._models_stats: Dict[str, str] = {}
        self._all_model_dict = {}

        self.n_jobs = 0
        self.hive = None

        self.build(feature_plan)
        print(f"RANDOM {get_random_sig()}")

    def build(self, bottom_layer: dict[str, tuple[str]]):
        """ Build the entire HTMPyramid bottom-up, creating the individual modules and connecting them appropriately """

        t0 = time.perf_counter()
        print("Building model... ")

        model_counter = 0
        model_dict = {}

        for layer_idx in range(self._n_layers):
            current_layer = bottom_layer if layer_idx == 0 else self._layer_dict[layer_idx]

            for node in current_layer:
                # get model input dim
                input_dims = self._get_input_dim(model_dict, layer_idx, node)

                # get config for this layer
                cfg_sp = get_layer_config(self.config['sp'].copy(), layer_idx)
                cfg_tm = get_layer_config(self.config['tm'].copy(), layer_idx)

                # apply any config modifications TODO: move to function?
                cfg_sp['potentialRadius'] = round(cfg_sp['potentialRadius'] * flatten_shape(input_dims).item())
                if isinstance(cfg_sp['columnDimensions'], (int, float)):
                    cfg_sp['columnDimensions'] = [cfg_sp['columnDimensions']]

                if not isinstance(cfg_tm['predictedSegmentDecrement'], float):
                    cfg_tm['predictedSegmentDecrement'] = 2 * cfg_tm['permanenceInc'] * cfg_sp['localAreaDensity']

                # init model
                model_counter += 1
                max_pool = self.max_pool[layer_idx] if isinstance(self.max_pool, tuple) else self.max_pool
                model = HTMModule(input_dims=input_dims,
                                  sp_cfg=cfg_sp,
                                  tm_cfg=cfg_tm,
                                  seed=self.seed * model_counter,
                                  anomaly_score=self.calc_anomaly,
                                  learn_schedule=self.lr_sch,
                                  max_pool=max_pool,
                                  flatten=self.flat,
                                  lazy_init=self.lazy_init,
                                  return_predictive=self.use_predictive,
                                  input_predictive=self.use_predictive and layer_idx > 0)

                model_dict[node] = model
                self._models_stats[node] = model.summary()
                if self.mp:
                    self.hive.add_model(key=node, model=model)

        # init all models
        if self.mp:
            self.hive.init_all_sp()
        else:
            if self.lazy_init:
                with mp.Pool(min(mp.cpu_count(), len(model_dict))) as pool:
                    results = list(tqdm(
                        pool.imap_unordered(init_model_mp, model_dict.items()),
                        total=len(model_dict),
                        desc="Initializing HTMs"))

                self._all_model_dict = dict(results)

            else:
                self._all_model_dict = model_dict

        print(f"Done in {time.perf_counter() - t0:.1f} sec")
        print(f"The model is {self._n_layers} layers deep with a total of {self._n_models} HTMs")
        time.sleep(0.5)

    def _get_input_dim(self, model_dict: dict, layer_idx: int, target: str) -> tuple[int, ...]:
        if layer_idx == 0:
            input_dims = self.ds.shape[target]
        else:
            inputs = list(self.graph.predecessors(target))
            if self.merge_mode == 'c':
                shapes = [model_dict[x].output_dim for x in inputs]
                input_dims = concat_shapes(*shapes, axis=self.concat_axis)
            else:
                input_dims = model_dict[inputs[0]].output_dim

        return input_dims

    def _fuse_layers_models(self, model_dict: Dict[str, HTMModule]):
        """ Fuse the model dict into the layered model-name dict """
        fused_dict = {}
        for idx, layer in self._layer_dict.items():
            fused_dict[idx] = {}
            for model_name in layer:
                fused_dict[idx][model_name] = model_dict[model_name]

        self._layer_dict = fused_dict

    def _run_layer(self, prev_layer_results: Dict[str, SDR | Tuple[SDR, np.ndarray]]) -> Dict[str, SDR]:
        """ Run the current layer (given previous layer results) with the hive and return the outputs """
        if self.mp:
            for model_key, sdr in prev_layer_results.items():
                self.hive.send_sdr(model_key, sdr)

            results = self.hive.collect_results()

        else:
            results = {}
            for model_key, sdr in prev_layer_results.items():
                model = self._all_model_dict[model_key]
                res = model(sdr)
                results[model_key] = res

        return results

    def _merge_layer_results(self, unmerged_results: Dict[str, SDR], next_layer: Dict[str, HTMModule]) -> Dict[
        str, SDR]:
        """ For each model in the next layer, get its inputs from the current layer's outputs and merge them """
        prev_layer_results = {}

        for parent_model in next_layer:
            to_merge = [unmerged_results[child] for child in self.graph.predecessors(parent_model)]

            if self.use_predictive:
                active, predictive = list(zip(*to_merge))
                merged_active = sdr_merge(*active, mode=self.merge_mode, axis=self.concat_axis)
                merged_predictive = sdr_merge(*predictive, mode=self.merge_mode, axis=self.concat_axis)
                merged = sdr_merge(merged_active, merged_predictive, mode='s')
            else:
                merged = sdr_merge(*to_merge, mode=self.merge_mode, axis=self.concat_axis)

            prev_layer_results[parent_model] = merged

        return prev_layer_results

    def run(self, iterations: int = None):
        """
        Run the HTMPyramid for `iterations` steps. (entire dataset by default)

        Note that the Pyramid can only run once, at which point the trained models are retrieved and the _workers killed.
        """
        print(f"RANDOM {get_random_sig()}")

        _iterations = len(self._ds) if iterations is None else min(len(self._ds), iterations)

        for ds_idx, item in tqdm(enumerate(self._ds), total=_iterations, desc="Running model"):
            if self._current_iter >= _iterations:
                break

            prev_layer_results = item
            for layer_idx in self._layer_dict:

                # run layer and get results per model
                unmerged_results = self._run_layer(prev_layer_results)

                # if not last layer (head)
                if layer_idx + 1 < self._n_layers:
                    next_layer = self._layer_dict[layer_idx + 1]
                    # find the results to merge and do it, store in `prev_layer_results`
                    prev_layer_results = self._merge_layer_results(unmerged_results, next_layer)

            self._current_iter += 1

        # get trained models back
        print("Requesting models.. ", end='')
        trained_models = self.hive.return_all_models() if self.mp else self._all_model_dict
        print("Returned")

        print("Fusing results.. ", end='')
        # fuse back into self dict, set head
        self._fuse_layers_models(trained_models)
        self._head = list(self._layer_dict[self._n_layers - 1].values()).pop()
        print("Fused")

        print("Terminating hive.. ", end='')
        # terminate processes
        if self.mp:
            self.hive.terminate()

        print("Done")
        print(f"RANDOM {get_random_sig()}")

    def model_summary(self):
        print()
        for idx, layer in self._layer_dict.items():
            print((idx == 0) * "INPUT " + "LAYER " + f"{idx}" * (idx > 0))
            longest = max(map(len, layer))
            max_pad = longest + 15
            for name in layer:
                dots = "." * (max_pad - len(name))
                summary = self._models_stats[name]
                print(f"{name}{dots}{summary}")

            print()

    def plot_results(self, gt=None, thresh=0.8):
        # TODO: fix issues with AL
        for layer_idx, layer_dict in self._layer_dict.items():
            fig, axs = plt.subplots(nrows=len(layer_dict), figsize=(15, 5 * len(layer_dict)))
            if len(layer_dict) == 1:
                axs = [axs]

            fig.suptitle(f"Layer {layer_idx}")
            for i, (name, model) in enumerate(layer_dict.items()):
                axs[i].set_title(name)
                scores = np.array(model.anomaly['score'])
                scores[scores <= thresh] = 0
                axs[i].plot(scores, label=f'Anomaly Score > {thresh}')
                if gt is not None:
                    axs[i].plot(gt, label='GT')

                if model == self.head:
                    likl = np.array(model.anomaly['likelihood'])
                    likl[likl <= thresh] = 0
                    axs[i].plot(likl, label='Anomaly Likelihood')

                axs[i].legend()

            fig.show()

    def get_scores(self, *, df: bool = False) -> Dict[str, np.ndarray] | pd.DataFrame:
        score_dict = {}
        for layer_idx, layer_dict in self._layer_dict.items():
            for name, model in layer_dict.items():
                sc_name = '_head_' if model == self.head else name
                score_dict[sc_name] = model.anomaly['score']

        if df:
            score_dict = pd.DataFrame(score_dict)

        return score_dict

    @property
    def ds(self) -> DataStreamer:
        return self._ds

    @property
    def fjs(self) -> str:
        return self._fjs

    @property
    def config(self) -> dict[str, dict]:
        return self._configuration.copy()

    @property
    def seed(self) -> int:
        return self._seed

    @property
    def head(self) -> HTMModule:
        return self._head

    @staticmethod
    def _redistribute_layer(current_layer: List[Tuple[str]]):
        single = current_layer.pop(-1)
        first = current_layer.pop(0)
        new_first = tuple(list(first) + list(single))
        return [new_first] + current_layer


def init_model_mp(key_model: Tuple[str, HTMModule]) -> Tuple[str, HTMModule]:
    key, model = key_model
    model._init_sp()
    model._init_tm()
    return key, model
