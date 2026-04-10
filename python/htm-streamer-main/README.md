## Setup Module

## 1) htm.core
### Pip
n/a
### Git
From command line:
* **Clone htm.core repo**: `git clone https://github.com/htm-community/htm.core.git`
* **CD to htm.core dir**: `cd htm.core`
* **Create fresh env**: `conda create -n htm_env python=3.9.7`
* **Switch to fresh env**: `conda activate htm_env`
* **Install packages**: `pip install -r requirements.txt`
* **Run setup.py**: `python setup.py install`

## 2) htm_streamer
### Pip
From command line:
* **Install packages**: `pip install git+https://github.com/gotham29/htm_streamer.git`
### Git
From command line:
* **Clone htm_streamer repo**: `git clone https://github.com/gotham29/htm_streamer.git`
* **CD to htm_streamer dir**:
  * `cd ..`
  * `cd htm_streamer`
* **Install packages**: `pip install -r requirements.txt`

## 3) Run Integration Tests
From command line:
* **Run**: `python tests/integration_tests.py`
* **Get results**: `htm_streamer/tests`

<br/>

## Quickstart

From Python:
* **Import Functions**:
  * `import os`
  * `import pandas as pd`
  * `from htm_source.utils.fs import load_config`
  * `from htm_source.pipeline.htm_batch_runner import run_batch`
* **Load Config & Data**:
  * `config_path = os.path.join(os.getcwd(), 'data', 'config.yaml')`
  * `data_path = os.path.join(os.getcwd(), 'data', 'batch', 'sample_timeseries.csv')`
  * `config = load_config(config_path)`
  * `data = pd.read_csv(data_path)`
* **Set Config**:
  * `timestep_tostop_sampling = 40`
  * `timestep_tostop_learning = 4000`
  * `timestep_tostop_running = 5000`
  * `model_for_each_feature = True`
  * `features_invalid = [ f for f in config['features'] if f not in data]`
  * `assert len(features_invalid) == 0, f"features not found --> {sorted(features_invalid)}"`
  * `config['timesteps_stop']['sampling'] = timestep_tostop_sampling`
  * `config['timesteps_stop']['learning'] = timestep_tostop_learning`
  * `config['timesteps_stop']['running'] = timestep_tostop_running`
  * `config['models_state']['model_for_each_feature'] = model_for_each_feature`
* **Train New HTM Models**:
  * `features_models, features_outputs = run_batch(cfg=config, config_path=None, learn=True, data=data, iter_print=100, features_models={})`
* **Run Existing HTM Models**:
  * `features_models, features_outputs = run_batch(cfg=config, config_path=None, learn=False, data=data, iter_print=100, features_models=features_models)`
* **Collect Models and Outputs**:
  * `f1 = my_features[0]`
  * `f1_model = features_models[ f1 ]`
  * `f1_anomaly_scores = features_outputs[f1]['anomaly_score']`
  * `f1_anomaly_liklihoods = features_outputs[f1]['anomaly_likelihood']`
  * `f1_prediction_counts = features_outputs[f1]['pred_count']`

<br/>

## Running quickstart-swat.py (SWaT Dataset)

Requires Python 3.13 and the local `htm.core` wheel built for `cp313`.

### First-time setup
From the `htm-streamer-main` directory:
```bash
./scripts/setup_venv.sh
```
This will:
* Create a Python 3.13 venv at `venv/`
* Install all dependencies from `requirements-py313.txt` (numpy>=2.0, compatible with Python 3.13)
* Install `htm.core` from the local wheel at `../htm.core/dist/htm-2.2.0-cp313-cp313-macosx_15_0_arm64.whl`
* Install `htm_source` as an editable package

### Run
```bash
./scripts/run_swat.sh
```

### Notes
* Data file expected at `data/swat_dataset.parquet`
* Config files used:
  * Model: `config/model/config--model_default.yaml`
  * Data: `config/data/config--swat.yaml`
* Build phase initializes 26 HTM models in parallel — expect ~2–3 min on first layers, longer on upper layers
* Run phase processes ~100,000 rows with a tqdm progress bar
* The venv uses `setuptools<71` to preserve `pkg_resources` compatibility with `htm.core`
