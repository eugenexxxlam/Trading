import os
import json
import numpy as np
from pprint import pprint
from prettytable import PrettyTable
import ray
from ray.rllib.algorithms import ppo
from ray.tune.registry import register_env, get_trainable_cls
from ray.rllib.utils.framework import try_import_torch

import argparse
import os

import ray
from ray import air, tune
from ray.tune.registry import get_trainable_cls
from gymnasium.spaces import Box, Discrete
import ray
from ray import air, tune
from ray.rllib.algorithms import ppo, impala
from ray.rllib.algorithms.ppo import PPOConfig
from ray.rllib.examples.env.stateless_cartpole import StatelessCartPole
from ray.tune.logger import pretty_print
from ray.rllib.algorithms.algorithm import Algorithm
from ray.tune.registry import register_env
from ray.rllib.utils.framework import try_import_tf, try_import_torch

START_TRADING_TIME = 9
END_TRADING_TIME = 21
MAX_NOP = 300_000
MAX_PROFIT_TARGET = 65
MAX_TIMESTEP = 1120
INITIAL_BALANCE = 100000
LEVERAGE = 100
RISK_FREE_RATE = 0.0003
UNIT = 1
CUT_LOSS = -25
STOP_OUT = 50
TRAILING_STOP = 0.1
HWM_DROP = 0.15

# python3 20240126_discrete_loading_impala_LSTM.py > output_20260108_300nop_100K_2012_Q1.txt

tf1, tf, tfv = try_import_tf()

torch, nn = try_import_torch()

from margin_env_20240126 import MarginTradingEnv

# List of data file paths
data_paths = [
    # '/workspaces/7_Docker/CSV/Majors/GBPUSD/output/2014_Q1.csv',
    '/home/ubuntu/dev_20231223/output_XAUUSD/2012_Q1.csv',
    # '/workspaces/7_Docker/CSV/Majors/GBPUSD/output/2013_Q3.csv',
    # '/workspaces/7_Docker/CSV/Majors/GBPUSD/output/2013_Q4.csv',
]


# base_path = '/workspaces/Docker_20230612/CSV/Majors/GBPUSD/output/'

# data_paths = []

# for year in range(2012, 2013): # Loop through years from 2005 to 2021
#     for quarter in range(1, 5): # Loop through quarters (Q1 to Q4)
#         data_paths.append(f'{base_path}{year}_Q{quarter}.csv')

# The data_paths list now contains all the paths you need from 2005 to 2021


# The path to the checkpoint you want to test
checkpoint_path = "/home/ubuntu/dev_20231223/20240126_checkpoint_003280"


# Default path for initialization
default_path = data_paths[0]

def env_creator(env_config):
    path = env_config.get("path", default_path)
    return MarginTradingEnv(path)

# Register the environment with Ray
register_env("MarginTradingEnv-v0", env_creator)

env_params = {
    "leverage": LEVERAGE,
    "initial_balance": INITIAL_BALANCE,
    "risk_free_rate": RISK_FREE_RATE,
    "unit": 1,
    "env_debug": False,
    "sim_debug": False
}

# General config for param_space
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
        # num_cpus_per_worker = 10,/home/ubuntu/20240108_checkpoint_contin/IMPALA_MarginTradingEnv-v0_83d41_00000_0_2024-01-07_08-26-54/checkpoint_00018
        # num_gpus_per_worker = 0.05,
       
    )
    .rollouts(num_rollout_workers=7)
)

def load_trained_model(checkpoint_path, config):
    algo = config.build()
    algo.restore(checkpoint_path)
    return algo

def print_trades_as_table(trades):
    table = PrettyTable()
    table.field_names = ["Time", "Side", "Symbol", "Price", "Quantity", "Trade Cost", "Trade Profit"]

    for trade in trades:
        table.add_row([
            trade["time"],
            trade["side"],
            trade["symbol"],
            trade["price"],
            trade["quantity"],
            f"{trade['trade_cost']:.2f}",
            f"{trade['trade_profit']:.2f}"
        ])

    print(table)

def print_records_as_table(records, OHLCV_df):
    table = PrettyTable()
    table.field_names = ["Timestep", "Timestamp", "Equity", "Balance", "NOP", "Realized PnL", "Unrealized PnL",
                         "Accumulated Cost", "Margin Required", "Margin Free", "Margin Level", 
                         "Portfolio Leverage", "Return Percentage", "Log Return", "Max Drawdown", 
                         "Sortino Ratio", "Reward", "Avg Price", "Spot Price", "Floating Profit", 
                         "Trade Not Executed", "Stopped Out"]

    for record in records:
        # Fetching timestamp from the index
        timestamp = OHLCV_df.index[record["timestep"]]

        table.add_row([
            record["timestep"],
            timestamp,
            f"{record['equity']:.2f}",
            f"{record['balance']:.2f}",
            f"{record['nop']:.2f}",
            f"{record['realized_pnl']:.2f}",
            f"{record['unrealized_pnl']:.2f}",
            f"{record['accumulated_cost']:.2f}",
            f"{record['margin_required']:.2f}",
            f"{record['margin_free']:.2f}",
            f"{record['margin_level']:.2f}",
            f"{record['portfolio_leverage']:.2f}",
            f"{record['return_percentage']:.2f}",
            f"{record['log_return']:.2f}",
            f"{record['max_drawdown']:.2f}",
            f"{record['sortino_ratio']:.2f}",
            f"{record['reward']:.2f}",
            f"{record['avg_price']:.2f}",
            f"{record['spot_price']:.2f}",
            f"{record['Floating_Profit']:.2f}",
            record["trade_not_executed"],
            record["stopped_out"]
        ])

    print(table)

def print_combined_trades_and_records_as_table(trades, records, OHLCV_df):
    table = PrettyTable()
    table.field_names = ["Timecount", "Timestep", "Timestamp", "Side", "Symbol", "Price", "Quantity", "Trade Cost", "Trade Profit",
                         "Equity", "Balance", "NOP", "Realized PnL", "Unrealized PnL",
                         "Accumulated Cost", "Margin Required", "Margin Free", "Margin Level",
                         "Portfolio Leverage", "Return Percentage", "Log Return", "Max Drawdown",
                         "Sortino Ratio", "Reward", "Avg Price", "Spot Price", "Floating Profit",
                         "Trade Not Executed", "Stopped Out"]

    # Create a dictionary for faster lookup of records by timestep
    records_by_time = {record['timestep']: record for record in records}

    for trade in trades:
        timestep = trade["time"]
        record = records_by_time.get(timestep)

        # Fetching timestamp from the index
        timestamp = OHLCV_df.index[timestep]

        # If there's a corresponding record for this trade, include it in the table
        if record:
            table.add_row([
                record["timecount"],
                timestep,
                timestamp,
                trade["side"],
                trade["symbol"],
                trade["price"],
                trade["quantity"],
                f"{trade['trade_cost']:.0f}",
                f"{trade['trade_profit']:.0f}",
                f"{record['equity']:.0f}",
                f"{record['balance']:.0f}",
                f"{record['nop']:.0f}",
                f"{record['realized_pnl']:.0f}",
                f"{record['unrealized_pnl']:.0f}",
                f"{record['accumulated_cost']:.2f}",
                f"{record['margin_required']:.2f}",
                f"{record['margin_free']:.2f}",
                f"{record['margin_level']:.2f}",
                f"{record['portfolio_leverage']:.2f}",
                f"{record['return_percentage']:.2f}",
                f"{record['log_return']:.2f}",
                f"{record['max_drawdown']:.2f}",
                f"{record['sortino_ratio']:.2f}",
                f"{record['reward']:.6f}",
                f"{record['avg_price']:.5f}",
                f"{record['spot_price']:.5f}",
                f"{record['Floating_Profit']:.0f}",
                record["trade_not_executed"],
                record["stopped_out"]
            ])

    print(table)

import tensorflow as tf

def run_trained_model(agent, env, num_episodes):
    policy = agent.get_policy()
    for i in range(num_episodes):
        state, info = env.reset()
        terminated = False
        truncated = False
        episode_reward = 0
        lstm_state = policy.get_initial_state()

        while not (terminated or truncated):
            state_flat = state.astype(np.float32)
            input_dict = {
                "obs": tf.reshape(tf.convert_to_tensor(state_flat), (1, -1)),
                "is_training": False,
            }
            # Using the action output directly
            action, new_lstm_state, _ = policy.compute_single_action(input_dict['obs'], state=lstm_state)
            
            # Assuming the new action space returns [terminated, truncated] as third and fourth elements
            state, reward, terminated, truncated, info = env.step(action)
            
            lstm_state = new_lstm_state
            episode_reward += reward
        
        print_combined_trades_and_records_as_table(info['closed_trades'], info['record'], env.OHLCV_df)
        
        pprint(info['account_info'])
        print(f"Episode {i + 1}, Reward: {episode_reward}")

# Load the trained model from the checkpoint
agent = load_trained_model(checkpoint_path, config)

# Loop through the data_paths
for data_path in data_paths:
    print(f"Evaluating on data from {data_path}")
    
    # Create the environment with the current data_path
    env = env_creator({"path": data_path})

    # Test the trained model
    num_episodes = 30
    run_trained_model(agent, env, num_episodes)