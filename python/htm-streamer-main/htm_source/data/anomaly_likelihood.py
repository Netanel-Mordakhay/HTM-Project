
import math


class AnomalyLikelihood:
    """
    This class analyses a time series of anomaly values (as returned by the
    temporal memory class) and determines the likelihood that an anomaly has
    actually occurred. This class assumes that the anomaly values are part of a
    normal distribution and so it tracks the mean and standard deviation of them
    using an exponentially weighted moving window.

    To use it simply create an instance and then feed it successive anomaly
    scores. Example:
        anomaly_history = AnomalyLikelihood()
        while True:
            temporal_memory.compute(...)
            anomaly_probability = anomaly_history.compute(temporal_memory.anomaly)
            if anomaly_probability > 0.25:
                print("Anomaly Detected!")
    """

    def __init__(self, period: int = 1000, min_mean: float = 0.03, min_var: float = 0.0003):
        """
        Argument period is the number of samples to use to calculate the
        distribution of anomaly scores.
        """
        self.period = float(period)
        self.alpha = 1.0 - math.exp(-1.0 / self.period)
        self.mean = 1.0
        self.var = 0.0003
        self.std = math.sqrt(self.var)
        self.prev = 0.0
        self.n_records = 0
        self.min_mean = min_mean
        self.min_var = min_var

    def compute(self, anomaly: float) -> float:
        """ Returns the probability that an anomaly has occurred.

        Calculates the log-likelihood of the probability that the given anomaly
        score is greater than a historical distribution of anomaly scores. Then
        updates the historical distribution to include the given anomaly score.
        """
        likelihood = self.get_likelihood(anomaly)
        self.add_record(anomaly)
        combined = 1 - (1 - likelihood) * (1 - self.prev)
        self.prev = likelihood

        if self.n_records < self.period:
            return 0.0

        return get_log_likelihood(combined)

    def add_record(self, anomaly: float):
        """
        """
        # http://web.archive.org/web/http://people.ds.cam.ac.uk/fanf2/hermes/doc/antiforgery/stats.pdf
        # Skip to Chapter 9.
        diff = anomaly - self.mean
        incr = self.alpha * diff
        self.mean = self.mean + incr
        self.var = (1.0 - self.alpha) * (self.var + diff * incr)

        # Handle edge case of almost no deviations and super low anomaly scores. We
        # find that such low anomaly means can happen, but then the slightest blip
        # of anomaly score can cause the likelihood to jump up to red.
        self.mean = max(self.mean, self.min_mean)
        # Catch all for super low variance to handle numerical precision issues
        self.var = max(self.var, self.min_var)

        self.std = math.sqrt(self.var)
        self.n_records += 1

        if self.n_records % 500 == 0:
            print(f"Iter: {self.n_records} \t Mean: {self.mean:.4f} \t Var: {self.var:.4f}")

    def get_likelihood(self, anomaly: float) -> float:
        """
        This returns "P(score >= distribution)" which is the probability that the
        given anomaly score is greater than or equal to a random number from this
        distribution.
        """
        # Calculate the Q function with the complementary error function, explained
        # here: http://www.gaussianwaves.com/2012/07/q-function-and-error-functions
        try:
            z = (anomaly - self.mean) / self.std
        except ZeroDivisionError:
            z = float('inf')
        return 1.0 - 0.5 * math.erfc(z / 1.4142)

def get_log_likelihood(likelihood: float) -> float:
    """
    Compute a log scale representation of the likelihood value. Since the
    likelihood computations return low probabilities that often go into four 9's
    or five 9's, a log value is more useful for visualization, thresholding,
    etc.
    """
    # The log formula is:
    #      Math.log(1.0000000001 - likelihood) / Math.log(1.0 - 0.9999999999)
    return math.log(1.0000000001 - likelihood) / -23.02585084720009
