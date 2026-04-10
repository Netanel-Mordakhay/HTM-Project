from __future__ import annotations

from abc import ABC
from typing import Tuple

import numpy as np

from htm.bindings.algorithms import SpatialPooler, TemporalMemory
from htm.bindings.sdr import SDR

from htm_source.utils.metric import calc_anomaly_score
from htm_source.utils.sdr import sdr_max_pool, flatten_shape, sdr_squeeze, sdr_flatten


class HTMBase(ABC):
    def __init__(self, *args, **kwargs):
        self._iteration = 0
        self._seed = None
        self._out_dims = None
        self._input_dims = None
        self._learning = True
        self._configuration = dict()
        self._anomaly_history = {'score': []}
        self._activity_history = {'sp': []}
        self._initialized_sp = False
        self.lr_sch = None
        self.sp = None
        self.tm = None
        self.max_pool = None
        self.flat = None

    def __call__(self, *args, **kwargs) -> SDR | Tuple[SDR, ...]:
        # pre-forward hooks
        self.learning_hook()

        ret_val = self.forward(*args, **kwargs)

        # post-forward hooks
        ret_val = self.post_forward(ret_val) if isinstance(ret_val, SDR) else tuple(map(self.post_forward, ret_val))
        self._iteration += 1
        return ret_val

    def _init_sp(self):
        raise NotImplementedError

    def _init_tm(self):
        raise NotImplementedError

    def post_forward(self, *args):
        return args

    def forward(self, *args, **kwargs) -> SDR | Tuple[SDR, ...]:
        raise NotImplementedError

    def train(self):
        self._learning = True

    def eval(self):
        self._learning = False

    def learning_hook(self):
        if self.lr_sch is None:
            return
        if self._iteration <= self.lr_sch:
            self.train()
        else:
            self.eval()

    @property
    def learning(self) -> bool:
        return self._learning

    @property
    def config(self) -> dict:
        return self._configuration.copy()

    @property
    def seed(self) -> int:
        return self._seed

    @property
    def timestep(self) -> int:
        return self._iteration

    @property
    def output_dim(self) -> Tuple[int]:
        return self._out_dims

    @property
    def input_dim(self) -> Tuple[int]:
        return self._input_dims

    @property
    def anomaly(self) -> dict:
        return self._anomaly_history.copy()

    @property
    def activity(self) -> dict:
        return self._activity_history.copy()


class HTMModule(HTMBase):
    def __init__(self, input_dims: tuple | list | np.ndarray,
                 sp_cfg: dict | None,
                 tm_cfg: dict,
                 seed: int,
                 max_pool: int = 1,
                 flatten: bool = False,
                 learn_schedule: int = None,
                 anomaly_score: bool = True,
                 lazy_init: bool = True,
                 return_predictive: bool = False,
                 input_predictive: bool = False):

        super().__init__()
        self._configuration = {'sp': sp_cfg, 'tm': tm_cfg}
        self._seed = seed
        self._input_dims = input_dims
        self._inp_pred = input_predictive
        self._ret_pred = return_predictive

        if not lazy_init:
            self._init_sp()
            self._init_tm()

        self._out_dims: tuple[int, ...] = tuple((*self.column_dim, self.config['tm']['cellsPerColumn']))
        self._rad = self.config['sp']['potentialRadius']
        self._boost = self.config['sp']['boostStrength']

        self.max_pool = 1 if max_pool in {False, None, 0} else max_pool
        self.flat = flatten
        self.calc_anomaly = anomaly_score
        self.lr_sch = learn_schedule

    def _init_sp(self):
        if self.config['sp'] is not None:
            self.sp = SpatialPooler(inputDimensions=self.input_dim,
                                    columnDimensions=self.column_dim,
                                    potentialPct=self.config["sp"]["potentialPct"],
                                    potentialRadius=self.config["sp"]["potentialRadius"],
                                    globalInhibition=self.config["sp"]["globalInhibition"],
                                    synPermInactiveDec=self.config["sp"]["synPermInactiveDec"],
                                    stimulusThreshold=self.config["sp"]["stimulusThreshold"],
                                    synPermActiveInc=self.config["sp"]["synPermActiveInc"],
                                    synPermConnected=self.config["sp"]["synPermConnected"],
                                    boostStrength=self.config["sp"]["boostStrength"],
                                    localAreaDensity=self.config['sp']['localAreaDensity'],
                                    wrapAround=self.config['sp']['wrapAround'],
                                    seed=self.seed)

    def _init_tm(self):
        self.tm = TemporalMemory(columnDimensions=self.column_dim,
                                 cellsPerColumn=self.config["tm"]["cellsPerColumn"],
                                 activationThreshold=self.config["tm"]["activationThreshold"],
                                 initialPermanence=self.config["tm"]["initialPerm"],
                                 connectedPermanence=self.config["tm"]["permanenceConnected"],
                                 minThreshold=self.config["tm"]["minThreshold"],
                                 maxNewSynapseCount=self.config["tm"]["newSynapseCount"],
                                 permanenceIncrement=self.config["tm"]["permanenceInc"],
                                 permanenceDecrement=self.config["tm"]["permanenceDec"],
                                 predictedSegmentDecrement=self.config["tm"]["predictedSegmentDecrement"],
                                 maxSegmentsPerCell=self.config["tm"]["maxSegmentsPerCell"],
                                 maxSynapsesPerSegment=self.config["tm"]["maxSynapsesPerSegment"],
                                 seed=self.seed,
                                 checkInputs=False)

    def forward(self, input_sdr: SDR, *, external_active: SDR = None, external_winning: SDR = None) -> SDR | Tuple[
        SDR, ...]:

        external_args = [external_active, external_winning] if (external_active is not None and
                                                                external_winning is not None) else []
        # SPATIAL POOLER (or just encoding)
        if self.sp:
            # Create an SDR to represent active columns
            active_columns = SDR(self.sp.getColumnDimensions())
            self.sp.compute(input_sdr, self.learning, active_columns)
        else:
            active_columns = input_sdr

        # log number of active columns for this iteration
        self._activity_history['sp'].append(int(len(active_columns.sparse)))

        # TEMPORAL MEMORY
        self.tm.activateDendrites(self.learning, *external_args)
        predictive_cells = self.tm.getPredictiveCells()
        predictive_columns = self.tm.cellsToColumns(predictive_cells)

        # calc anomaly
        if self.calc_anomaly:
            anomaly = calc_anomaly_score(active_columns, predictive_columns)
            self._anomaly_history['score'].append(anomaly)

        self.tm.activateCells(active_columns, self.learning)
        active_cells = self.tm.getActiveCells()

        # remove 1st dim in case its (1, ...)
        if self._inp_pred:
            active_cells = sdr_squeeze(active_cells, axes=0)
            predictive_cells = sdr_squeeze(predictive_cells, axes=0)

        output = (active_cells, predictive_cells) if self._ret_pred else active_cells

        return output

    def post_forward(self, x: SDR) -> SDR:
        x = sdr_max_pool(x, ratio=self.max_pool)
        x = sdr_flatten(x) if self.flat else x
        return x

    @property
    def column_dim(self) -> tuple[int]:
        return tuple(([1, *self.config["sp"]["columnDimensions"]] if self._inp_pred else self.config["sp"]["columnDimensions"])
                        if self.config['sp'] else self.input_dim)

    @property
    def output_dim(self) -> tuple[int]:
        ret_val = self._out_dims
        if self.max_pool not in (None, False, 1, 0):
            ret_val = tuple((ret_val[0] // self.max_pool, *ret_val[1:]))
        if self.flat:
            ret_val = flatten_shape(ret_val)
        if self._ret_pred:
            ret_val = tuple((2, *ret_val))
        return ret_val

    def summary(self) -> str:
        return f"[{self._input_dims} --> {self.output_dim}] (max_pool: {self.max_pool}, R: {self._rad}, Boost: {self._boost:.2f})"
