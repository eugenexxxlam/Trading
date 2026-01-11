import argparse
import argparse
import os
import pandas as pd
import ray
import tensorflow as tf

from ray import air, tune
from ray.rllib.algorithms import ppo, impala
from ray.tune.registry import register_env
from ray.rllib.utils.framework import try_import_tf, try_import_torch

START_TRADING_TIME = 1
END_TRADING_TIME = 21
MAX_NOP = 15_000_000
MAX_PROFIT_TARGET = 120
MAX_TIMESTEP = 1120
INITIAL_BALANCE = 100000
LEVERAGE = 100
RISK_FREE_RATE = 0.0003
UNIT = 1
CUT_LOSS = -25
STOP_OUT = 50
TRADING_FEE = 0.0006
BATCH_INTERVAL = 15

tf1, tf, tfv = try_import_tf()
tf.compat.v1.enable_eager_execution()
torch, nn = try_import_torch()
NUM_WORKERS = 195

path = '/home/ubuntu/CSV/Metals/BTCUSD/output/2015_Q1.csv'

local_dir = '/home/ubuntu'
experiment_name = '20260108_cypto_non_hedge4'

from custom_env import MarginTradingEnv

def env_creator(_):
    return MarginTradingEnv(path)

register_env("MarginTradingEnv-v0", env_creator)

env_params = {
    # "file_path": 'path_to_your_file',  # specify the file path here
    "leverage": LEVERAGE,
    "initial_balance": INITIAL_BALANCE,
    "stop_out_level": STOP_OUT,  # specify the stop_out_level here
    "risk_free_rate": RISK_FREE_RATE,
    "unit": UNIT,
    "env_debug": False,
    "sim_debug": False
}

# GeneraVvq bq11l config for param_space
config = (
    impala.ImpalaConfig()
    .environment(
        "MarginTradingEnv-v0",
        env_config=env_params,
    )
    .training(
        # lr_schedule: Optional[List[List[Union[int, float]]]] = NotProvided,
        # use_critic = True,
        # use_gae  = True,
        lr =  0.00027565251390671903,
        gamma = 0.9403961134522817,
        # sgd_minibatch_size = 512,
        train_batch_size = 4096,
        num_sgd_iter =  16,
        vf_loss_coeff = 0.02151033534088739,
        # clip_param =  0.25515184721404843,
        # kl_coeff=0.0,
        entropy_coeff=0.01,
            
        # LSTM
        model={
            "use_lstm": True,  # Set to True if you want to use LSTM, and False otherwise
            # "lstm_use_prev_action": True, 
            # "lstm_use_prev_reward": True, 
            # "_disable_action_flattening": True,
            "max_seq_len": 20,  # Sequence length for LSTM
            "lstm_cell_size": 512,  # LSTM cell size
            
            # "use_attention": True,
        },
    )
    .framework("tf2",
               eager_tracing=True
               )
    .resources(
        num_gpus=1,
        # num_cpus_per_worker = 10,
        # num_gpus_per_worker = 0.05,
       
    )
    .rollouts(num_rollout_workers=NUM_WORKERS)
)

stop = {
    # "training_iteration": args.stop_iters,
    "timesteps_total": 1e888,
    "episode_reward_mean": 1e888,
}

ray.init()

# Run tune for some iterations and generate checkpoints.

tuner = tune.Tuner(
    "IMPALA",
    param_space=config.to_dict(),
    
    run_config=air.RunConfig(
        name=experiment_name,
        storage_path=local_dir,
        stop=stop, 
        verbose=1,
        checkpoint_config=air.CheckpointConfig(
                                               checkpoint_frequency=20,
                                               checkpoint_at_end=True
                                               )       
    ),
)

checkpoint_dir = local_dir + "/" + experiment_name

# tuner = tune.Tuner.restore(
#     path=checkpoint_dir,
#     trainable="IMPALA",
    
#     resume_errored=True,
#     resume_unfinished=True,
       
# )


results = tuner.fit()

