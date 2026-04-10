class AnomalyDetector:
    def __init__(self, threshold: float, min_seen: int, max_lost: int, learn_period: int):

        self.learn_period = learn_period
        self.thresh = threshold
        self.min_see = min_seen
        self.max_lost = max_lost

        self.last_idx_returned = -1
        self.idx = -1
        self.accumulator = []
        self.anomaly = None
        self.waiting = 0
        self.counter = 0

    def conclude(self) -> None | tuple:
        self.decide(anomaly=False)
        return self._predict()

    def update(self, score: float) -> None | tuple:
        self.idx += 1

        if self.idx < self.learn_period:
            score = 0

        if score >= self.thresh:
            # if score is alarming
            self.anomaly_observe()

            # if in a state of anomaly, can return now
            if self.anomaly:
                return self._predict()

        else:
            # if score not alarming
            if not (self.counter > 0 and self.wait()):
                # and we are not waiting, no anomaly
                self.normal_observe()
                return self._predict()

    def anomaly_observe(self):
        self.counter += 1
        self.waiting = 0
        if self.counter >= self.min_see:
            self.decide(anomaly=True)

    def normal_observe(self):
        self.counter = 0
        self.waiting = 0
        self.decide(anomaly=False)

    def wait(self):
        if self.waiting < self.max_lost and self.anomaly:
            self.waiting += 1
            # self.counter -= 1
            return True
        else:
            return False

    def decide(self, *, anomaly: bool):
        self.anomaly = anomaly
        num_waiting_predictions = self.idx - self.last_idx_returned
        self.accumulator.extend([1 if anomaly else 0] * num_waiting_predictions)

    def _predict(self) -> tuple:
        ret_val = self.accumulator

        self.last_idx_returned = self.idx
        self.accumulator = []

        return tuple(ret_val)
