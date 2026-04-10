from datetime import datetime
from typing import Union, Tuple, Iterable

from htm.bindings.sdr import SDR

from htm_source.data.encoding import EncoderFactory
from htm_source.data.types import HTMType


class Feature:
    def __init__(self, name: str, params: dict, seed: int):
        """
        This is a wrapper class for easy type checking and encoding of features
        """
        self._name = name
        params['seed'] = seed * EncoderFactory.get_encoder_idx()
        self._seed = params['seed']
        self._type = params['type']
        self._params = params
        self._encoder = EncoderFactory.get_encoder(self.params)

        if self.type is HTMType.Datetime:
            try:
                self._dt_format = self.params['format']
            except KeyError:
                raise ValueError(f"Datetime-like feature `{self.name}` must have a `format` parameter")

    def encode(self, data: Union[str, int, float, datetime, SDR]) -> SDR:
        """
        Encodes input `data` with the appropriate encoder, based on `params` given in init
        """
        if self.type is HTMType.Datetime and not isinstance(data, datetime):
            data = datetime.strptime(data, self._dt_format)

        return self._encoder.encode(data)

    def __eq__(self, other) -> bool:
        return self.name == other.name

    def __repr__(self) -> str:
        return f"Feature(name={self.name}, params={self.params}, seed={self.seed})"

    @property
    def params(self) -> dict:
        return self._params.copy()

    @property
    def type(self) -> HTMType:
        return self.params['type']

    @property
    def name(self) -> str:
        return self._name

    @property
    def seed(self) -> int:
        return self._seed

    @property
    def encoding_size(self) -> int:
        return self._encoder.size

    @property
    def encoding_dim(self) -> Tuple[int]:
        return self._encoder.dimensions


def separate_time_and_rest(features: Iterable[Feature], strict: bool = True) -> Tuple[Union[None, str], Tuple[str, ...]]:
    """
    Given any iterable of Features, will separate the time-like feature from the rest and return the feature names:
    time_feature, (other_f_1, ...)

    If `strict` is set to True, will raise an exception if more than 1 time-like feature is found
    """
    time = None
    non_time = list()
    for feat in features:
        if feat.type is HTMType.Datetime:
            if strict and time is not None:
                raise ValueError(f"More than a single time-like feature found: {time, feat.name}")
            else:
                time = feat.name
        else:
            non_time.append(feat.name)

    return time, tuple(non_time)
