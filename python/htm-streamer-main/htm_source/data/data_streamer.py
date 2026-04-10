from __future__ import annotations

import multiprocessing as mp
import time
from typing import Mapping
from warnings import warn

import numpy as np
import pandas as pd
from htm.bindings.sdr import SDR

from htm_source.config import build_enc_params
from htm_source.data import Feature
from htm_source.data.encoding import EncoderFactory
from htm_source.utils import dict_zip
from htm_source.utils.sdr import concat_shapes, sdr_merge


class DataStreamer:

    def __init__(self,
                 df: pd.DataFrame,
                 *,
                 features_cfg: dict,
                 encoders_cfg: dict,
                 seed: int,
                 merge_plan: dict,
                 train_percent: float = 1.0,
                 feature_merge_mode: str = None,
                 prepare: bool = True,
                 feature_join_str: str = '+'):
        """
        TODO
        """
        EncoderFactory.reset_encoder_idx()
        self._dataframe = df.reset_index(drop=True)
        self._prepared = False
        self._encoded_data = None
        self._fjs = feature_join_str

        self._params = build_enc_params(features_cfg=features_cfg,
                                        encoders_cfg=encoders_cfg,
                                        data_samples=df.iloc[:int(len(df) * train_percent)])
        self.features: dict[str, Feature] = {name: Feature(name, params.copy(), seed=seed) for name, params in
                                             self._params.items()}

        self._mode = feature_merge_mode
        self._plan = merge_plan
        self._check_merge_plan()  # check missing features and typing

        self._encoding_width = self._get_encoding_width()

        if prepare:
            _s = time.perf_counter()
            print("Encoding data.. ", end='')
            with mp.Pool(mp.cpu_count()) as pool:
                self._encoded_data = pool.map(self.__getitem__, range(len(self)))
            print(f"Done in {time.perf_counter() - _s:.3f} sec")
            self._prepared = True

    def __len__(self) -> int:
        return len(self._dataframe)

    def __getitem__(self, idx: int) -> SDR | dict[str, SDR]:
        if self._prepared:
            item = self._encoded_data[idx]
        else:
            row = self._dataframe.iloc[idx]
            item = self.get_encoding(row)

        return item

    def get_encoding(self, data_row: Mapping) -> dict[str, SDR]:
        """ Encode the feature or features in the given dataframe row into SDR(s)
            Returns a dict mapping feature name (or names if concatenated as a group) to SDR """

        final_encoding = dict()

        # get encoding for each feature separately
        temp_encoding = {f_name: feature.encode(data) for f_name, data, feature in dict_zip(data_row, self.features)}

        # for each combination in plan
        for l_name, features in self.plan.items():
            # merge this combination of features into a new encoding according to mode
            temp_sdrs = [temp_encoding[feat] for feat in features]
            new_encoding = sdr_merge(*temp_sdrs, mode=self._mode) if self._mode is not None else temp_sdrs[0]
            final_encoding[l_name] = new_encoding

        return final_encoding

    def _get_encoding_width(self) -> dict[str, tuple[int]]:
        """ Returns a dict mapping feature name to encoding width """

        feature_shapes = dict()

        for entry, features in self.plan.items():
            shapes = [self.features[f_name].encoding_dim for f_name in features]

            # not merging means only 1 feature
            if self._mode is None:
                length = shapes.pop()

            # concat supports different shapes
            elif self._mode == 'c':
                length = concat_shapes(*shapes)

            # for other modes, all shapes must be the same
            else:
                assert len(set(shapes)) == 1, (f"Shapes `{shapes}` for features `{features}` are incompatible with "
                                               f"chosen merge mode `{self._mode}`")

                length = shapes[0]  # all shapes the same

            feature_shapes[entry] = length

        return feature_shapes

    def _check_merge_plan(self):
        features_distinct = set()

        for l_name, features in self.plan.items():
            # check typing
            if not (isinstance(features, tuple) and all([isinstance(f, str) for f in features])):
                raise TypeError(f"plan features must be provided as a tuple of strings, got: {features} for `{l_name}`")

            # check cases for single features
            if self._mode is None and len(features) > 1:
                raise ValueError(f"Layer {l_name} has more than 1 feature: {features} but merger mode is None")

            elif self._mode is not None and len(features) == 1:
                warn(f"Merger mode is not None, but `{l_name}` has only 1 feature")

            for feat in features:
                if self._fjs in feat:
                    raise AssertionError(f"Feature names cannot contain `{self._fjs}`, got: `{feat}`.\nIf this is an "
                                         f"issue, change the `feature_join_str` parameter")

            features_distinct.update(features)

        if missing1 := set(self.features.keys()).difference(features_distinct):
            warn(f"Not all given features are present in feature plan, missing: {missing1}")

        if missing2 := features_distinct.difference(set(self.features.keys())):
            raise RuntimeError(f"Not all features in plan are present in data config: {missing2}")

    def raw_data(self, idx: int) -> np.ndarray:
        return self._dataframe.iloc[idx].values.copy()

    def get_encoding_summary(self) -> dict:
        summary = {}
        for feat in self.features.values():
            params = feat.params
            params['seed'] = feat.seed
            params['type'] = params['type'].name
            summary[feat.name] = params

        return summary

    @property
    def plan(self) -> dict[str, tuple[str]]:
        return self._plan

    @property
    def shape(self) -> dict[str, tuple[int]]:
        return self._encoding_width

    @property
    def fjs(self) -> str:
        return self._fjs
