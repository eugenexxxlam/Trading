
class MarginTradeSimulator:
    def __init__(self, price_series, leverage=LEVERAGE, initial_balance=INITIAL_BALANCE, stop_out_level=STOP_OUT, risk_free_rate=RISK_FREE_RATE, sim_debug=False):
        # Price series
        self.price_series = price_series
        # self.spot_prcie = 0
        self.timestep = 0
        self.timecount = 0
                
        # Account balance and equity
        self.pattern = self.generate_balance_pattern()
        # self.initial_balance = self.get_random_balance()
        self.initial_balance = initial_balance
        self.balance = initial_balance
        self.equity = initial_balance
        
        # Profit and Loss
        self.unrealized_pnl = 0
        self.realized_pnl = 0
        
        # Positions
        self.nop = 0
        self.avg_price = 0
        self.spot_price = 0
        
        # Leverage and margin
        self.leverage = leverage
        self.margin_required = 0
        self.margin_free = self.equity - self.margin_required
        self.margin_level = 100
        self.stop_out_level = stop_out_level
        self.portfolio_leverage = abs(self.nop) / self.equity if self.equity != 0 else 0
        
        # Performance metrics
        self.risk_free_rate = risk_free_rate
        self.equity_history = [initial_balance]
        self.max_drawdown = 0
        self.sortino_ratio = 0
        self.return_percentage = 0
        self.log_return = 0
        self.accumulated_cost = 0
        
        # Trading details
        self.trade_not_executed = False
        self.invalid_action = False
        self.stopped_out = False
        self.goal_reached = False
        self.sim_debug = sim_debug
        self.trade_cost = 0
        self.trade_profit = 0
        
        # reward parameters
        self.reward = 0
        
        self.realized_pnl_history = []
        
        self.timestamp = None # Initialize timestamp
        
        self.episode_start_index = 0

        self.reset()
            
    def reset(self):
        # Price series
        self.timestep = 0
        self.timecount = 0
        self.episode_start_index = 0
        self.timestamp = None # Initialize timestamp
        
        self.spot_price = 0

        # Account balance and equity
        self.initial_balance = self.initial_balance
        self.balance = self.initial_balance
        self.equity_history = []
        self.reward = 0

        # Profit and Loss
        self.realized_pnl = 0
        self.unrealized_pnl = 0

        # Positions
        self.nop = 0
        self.avg_price = 0
        self.done_trades = []

        # Leverage and margin
        self.margin_required = 0
        self.margin_free = self.balance
        self.margin_level = 100
        self.portfolio_leverage = 0

        # Performance metrics
        self.return_percentage = 0
        self.log_return = 0
        self.max_drawdown = 0
        self.sortino_ratio = 0
        self.accumulated_cost = 0
               
        # Trading details
        self.trade_not_executed = False

        self.stopped_out = False
        self.goal_reached = False
               
        self.account_info = {
            "timestep": self.timestep,
            "equity": self.equity,
            "balance": self.balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
            "reward": self.reward,
        }
        
        self.previous_account_info = {
            "timestep": 0,
            "equity": self.initial_balance,
            "balance": self.initial_balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
        }

        self.open_positions = {
            "nop": self.nop,
            "avg_price": self.avg_price,
            "spot_price": self.spot_price,
            "Floating_Profit": self.unrealized_pnl,
            "trade_not_executed": 1 if self.trade_not_executed else 0,
            "stopped_out": 1 if self.stopped_out else 0
        }
        
        self.done_trades = []

        self.update_account_info_and_open_positions()
        
        self.record = []
        
        # self.print_summary()

    def generate_balance_pattern(self):
        pattern = [10_000]
        while pattern[-1] < 1_000_000:
            # Multiply the last value by 5 if it's divisible by 10_000, else by 2
            next_value = pattern[-1] * 5 if pattern[-1] % (5 * 10_000) == 0 else pattern[-1] * 2
            pattern.append(next_value)
        return pattern

    def get_random_balance(self):
        # Select a random value from the pattern
        return random.choice(self.pattern)

    def is_end_of_day(self):
        return self.timecount % MAX_TIMESTEP == 0

    def execute(self, action, price, volume):
        # self.timecount += 1
        
        self.spot_price = price
                
        self.trade_not_executed = False  # Reset the trade_not_executed attribute
        self.previous_account_info = self.account_info.copy()
        
        margin_impact = self.calculate_margin_impact(action, price, volume)
        margin_level_after_trade = self.account_info["margin_level"] + margin_impact["margin_level_change"]
        margin_free_after_trade = self.account_info["margin_free"] - margin_impact["margin_required_change"]

        if margin_level_after_trade >= self.stop_out_level and margin_free_after_trade > 0:
            # Execute the trade action (Hold/Long/Short)
            if action == 0:  # Hold
                self.hold(price)
            elif action == 1:  # Long
                self.long(price, volume)
            elif action == 2:  # Short
                self.short(price, volume)
        else:
            if self.sim_debug:
                print("Trade not executed. Insufficient free margin or margin level would fall below stop-out level.")
            self.trade_not_executed = True
            self.hold(price)    # hold method will update acc_info with price
                           
        self.reward = self.calculate_reward()
        self.account_info["reward"] = self.reward
            
        self.create_record()       
        
    def check_goal(self, price):
        self.spot_price = price
        self.unrealized_pnl = self.calculate_unrealized_pnl()
        self.update_account_info_and_open_positions()
        
        # Check if goal reached
        if self.account_info["return_percentage"] > MAX_PROFIT_TARGET:
            self.goal_reached = True
            self.close(price)
            
        if self.sim_debug:
            print("Goal reached. Closing all positions.")
            
    def check_stopped_out(self, price):
        self.spot_price = price
        self.unrealized_pnl = self.calculate_unrealized_pnl()
        self.update_account_info_and_open_positions()
        
        # Check if stop out occurred
        # if self.account_info["margin_level"] < self.stop_out_level:
        if self.return_percentage < CUT_LOSS:
            self.stopped_out = True
            self.close(price)
            
        if self.sim_debug:
            print("Stop-out event. Closing all positions.")
            
    def calculate_margin_impact(self, action, price, volume):
        # Calculate the margin required for the potential trade
        margin_required_trade = self.calculate_margin_required(price, volume)

        # Estimate the new margin required after the trade
        margin_required_new = self.account_info["margin_required"]
        if action == 1 or action == 2:  # Long or Short
            margin_required_new += margin_required_trade
        elif action == 3:  # Close
            margin_required_new = 0

        # Estimate the new equity after the trade
        equity_new = self.equity
        if action == 3:  # Close
            equity_new += self.unrealized_pnl

        # Calculate the new margin free and margin level
        margin_free_new = equity_new - margin_required_new
        margin_level_new = (equity_new / margin_required_new) * 100 if margin_required_new != 0 else float("inf")

        return {
            "margin_required_change": margin_required_new - self.account_info["margin_required"],
            "margin_free_change": margin_free_new - self.account_info["margin_free"],
            "margin_level_change": margin_level_new - self.account_info["margin_level"]
        }
        
    def generate_trading_cost(self, price, volume):
        trading_cost_percentage = TRADING_FEE
        trading_cost = trading_cost_percentage * abs(volume)
        return trading_cost

    def hold(self, price):
        self.spot_price = price
        self.unrealized_pnl = self.calculate_unrealized_pnl()
        self.update_account_info_and_open_positions()
        
        if self.sim_debug:
            print(f"Hold and update the price {price: ,.5f}")
        
        return {
        "done_trades": self.done_trades,
        "open_positions": self.open_positions,
        "account_info": self.account_info,
    }
    
    def long(self, price, vol):
        realized_pnl = 0

        # Case 1: when nop = 0
        if self.nop == 0:
            self.trade_profit = 0
            
            self.trade_cost = self.generate_trading_cost(price, vol)
            
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop = vol
            self.avg_price = price
            self.spot_price = price
            self.unrealized_pnl = 0

        # Case 2: when nop > 0
        elif self.nop > 0:
            self.trade_profit = 0
            self.trade_cost = self.generate_trading_cost(price, vol)
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop += vol
            self.avg_price = (self.avg_price * (self.nop - vol) + price * vol) / self.nop
            self.spot_price = price
            self.unrealized_pnl = (price - self.avg_price) * self.nop

        # Case 3: when nop < 0 (position changes from short to long)
        else:
            # Case 3a: vol > abs(nop)
            if vol > abs(self.nop):
                remaining_vol = vol - abs(self.nop)

                # Close short position
                realized_pnl = (self.avg_price - price) * (abs(self.nop) / price)
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades for closing short position
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, abs(self.nop))
                self.accumulated_cost += self.trade_cost
                self.create_trade("buy", price, abs(self.nop), self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions for closing short position
                self.nop = remaining_vol
                self.avg_price = price

            # Case 3b: vol < abs(nop)
            elif vol < abs(self.nop):
                # Calculate realized P&L
                realized_pnl = (self.avg_price - price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop += vol  # Reduce the magnitude of the short position
                # Do not change the average price as it's still the same short position

            # Case 3c: vol = abs(nop)
            else:
                # Calculate realized P&L
                realized_pnl = (self.avg_price - price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop = 0
                self.avg_price = 0

        # Update open_positions
        self.spot_price = price
        self.unrealized_pnl = (price - self.avg_price) * self.nop

        self.update_account_info_and_open_positions()
        
        if self.sim_debug:
            print(f"Long at {price: ,.5f} for {vol: ,.2f} with trading cost {self.trade_cost: ,.2f}")

        return {
                "done_trades": self.done_trades,
                "open_positions": self.open_positions,
                "account_info": self.account_info,
            }

    def short(self, price, vol):
        realized_pnl = 0

        # Case 1: when nop = 0
        if self.nop == 0:
            self.trade_profit = 0
            self.trade_cost = self.generate_trading_cost(price, vol)
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop = -vol
            self.avg_price = price
            self.spot_price = price
            self.unrealized_pnl = 0

        # Case 2: when nop < 0
        elif self.nop < 0:
            self.trade_profit = 0
            self.trade_cost = self.generate_trading_cost(price, vol)
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop -= vol
            self.avg_price = ((abs(self.nop) + vol) * self.avg_price - price * vol) / abs(self.nop)
            self.spot_price = price
            self.unrealized_pnl = (self.avg_price - price) * abs(self.nop)

        # Case 3: when nop > 0 (position changes from long to short)
        else:
            # Case 3a: vol > nop
            if vol > self.nop:
                remaining_vol = vol - self.nop

                # Close long position
                realized_pnl = (price - self.avg_price) * abs(self.nop) / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades for closing long position
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, self.nop)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, self.nop, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions for closing long position
                self.nop = 0
                self.avg_price = 0

                # Open new short position with remaining_vol
                self.nop = -remaining_vol
                self.avg_price = price

                # Update done_trades for opening new short position
                self.trade_profit = 0
                self.trade_cost = self.generate_trading_cost(price, remaining_vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, remaining_vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

            # Case 3b: vol < nop
            elif vol < self.nop:
                # Calculate realized P&L
                realized_pnl = (price - self.avg_price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop -= vol  # Reduce the magnitude of the long position
                # Do not change the average price as it's still the same long position

            # Case 3c: vol = nop
            else:
                # Calculate realized P&L
                realized_pnl = (price - self.avg_price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop = 0
                self.avg_price = 0

        # Update open_positions
        self.spot_price = price
        self.unrealized_pnl = (price - self.avg_price) * self.nop
        

        self.update_account_info_and_open_positions()
        
        if self.sim_debug:
            print(f"Short at {price: ,.5f} for {vol: ,.2f} with trading cost {self.trade_cost: ,.2f}")

        return {
                "done_trades": self.done_trades,
                "open_positions": self.open_positions,
                "account_info": self.account_info,
            }

    def close(self, price):
        if self.nop == 0:
            return
        
        if self.nop > 0:
            self.short(price, abs(self.nop))
        elif self.nop < 0:
            self.long(price, abs(self.nop))
        
        # Update the reward after closing the positions
        self.reward = self.calculate_reward()
        self.account_info["reward"] = self.reward
        
        # Create a record of the current state after closing the positions
        self.create_record()

    def create_trade(self, side, price, quantity, trade_profit, trade_cost):
        
        trade = {
            "time": self.timestep,
            "symbol": "contract",
            "side": side,
            "price": price,
            "quantity": quantity,
            "trade_profit": trade_profit,
            "trade_cost": self.trade_cost,
        }
        self.done_trades.append(trade)
        
    def create_record(self):
        
        record_entry = {
            "timecount": self.timecount,
            "timestamp": self.timestamp,
            "timestep": self.timestep,
            "equity": self.equity,
            "balance": self.balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
            "reward": self.reward,
            "avg_price": self.avg_price,
            "spot_price": self.spot_price,
            "Floating_Profit": self.unrealized_pnl,
            "trade_not_executed": 1 if self.trade_not_executed else 0,
            "stopped_out": 1 if self.stopped_out else 0
        }
        self.record.append(record_entry)

    def update_timestamp(self, timestamp):
        self.timestamp = timestamp

    def check_margin_level(self):
        if self.equity == 0 or self.margin_required == 0:
            self.margin_level = 100
        else:
            self.margin_level = (self.equity / self.margin_required) * 100

    def update_account_info_and_open_positions(self):
       
        self.equity = self.balance + self.unrealized_pnl
        self.margin_required = self.calculate_margin_required(self.spot_price, abs(self.nop))
        self.margin_free = self.equity - self.margin_required

        self.check_margin_level()  # Add this line to update margin_level

        self.portfolio_leverage = abs(self.nop) / self.equity if self.equity != 0 else 0

        # Calculate return and log return
        self.return_percentage = (self.equity - self.initial_balance) / self.initial_balance * 100
        if self.equity > 0 and self.initial_balance > 0:
            self.log_return = np.log(self.equity / self.initial_balance)
        else:
            self.log_return = 0  # Or any other value that makes sense in this context

        # Calculate Sortino ratio (considering risk-free rate)
        if len(self.equity_history) > 1:
            returns = np.array(self.equity_history)
            daily_returns = np.diff(returns) / returns[:-1]
            daily_excess_returns = daily_returns - self.risk_free_rate / 252

            # Calculate downside deviation (volatility of negative excess returns)
            negative_excess_returns = daily_excess_returns[daily_excess_returns < 0]
            
            if len(negative_excess_returns) > 1:
                downside_deviation = np.std(negative_excess_returns)
            else:
                downside_deviation = 0

            if downside_deviation != 0:
                self.sortino_ratio = np.mean(daily_excess_returns) / downside_deviation
            else:
                self.sortino_ratio = 0
        else:
            self.sortino_ratio = 0

        # Calculate drawdown
        running_max_equity = np.maximum.accumulate(self.equity_history)
        drawdowns = (running_max_equity - self.equity_history) / running_max_equity
        self.max_drawdown = np.max(drawdowns) * 100 if len(drawdowns) > 0 else 0
        
        self.avg_price = self.calculate_avg_price()
                       
        self.account_info = {
            "timestep": self.timestep,
            "equity": self.equity,
            "balance": self.balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
            "reward": self.reward,
        }
        
        self.open_positions = {
            "nop": self.nop,
            "avg_price": self.avg_price,
            "spot_price": self.spot_price,
            "Floating_Profit": self.unrealized_pnl,
            "trade_not_executed": 1 if self.trade_not_executed else 0,
            "stopped_out": 1 if self.stopped_out else 0
        }

        self.equity_history.append(self.equity)
            
    def calculate_nop(self):
        """
        Calculates the net open position (NOP) from the list of done trades.
        """
        total_quantity = 0

        for trade in self.done_trades:
            if trade["side"] == "buy":
                total_quantity += trade["quantity"]
            else:
                total_quantity -= trade["quantity"]

        return total_quantity
        
    def calculate_avg_price(self):
        """
        Calculates the average price of all open positions.
        """
        total_notional = 0
        total_quantity = 0

        for trade in self.done_trades:
            if trade["side"] == "buy":
                total_notional += trade["price"] * trade["quantity"]
                total_quantity += trade["quantity"]
            else:
                total_notional -= trade["price"] * trade["quantity"]
                total_quantity -= trade["quantity"]

        if total_quantity == 0:
            return 0

        return total_notional / total_quantity

    def calculate_unrealized_pnl(self):
        """
        Calculates the unrealized P&L for all open positions.
        """
        if self.nop > 0:
            # Long position
            return (self.spot_price - self.avg_price) * (abs(self.nop) / self.spot_price)
        elif self.nop < 0:
            # Short position
            return (self.avg_price - self.spot_price) * (abs(self.nop) / self.spot_price)
        else:
            # No open positions
            return 0

    def calculate_margin_required(self, price, volume):
        margin_required = (abs(volume)) / self.leverage
        return margin_required

    def calculate_portfolio_leverage(self):
        """
        Calculate the portfolio leverage.
        Leverage = (Total Exposure) / Equity
        Total Exposure = NOP * Spot Price
        """
        if self.equity == 0:
            return 0

        total_exposure = abs(self.nop) * self.spot_price
        leverage = total_exposure / self.equity
        return leverage

    def set_reward_coefficients(self, **kwargs):
        for key, value in kwargs.items():
            if key in self.reward_coefficients:
                self.reward_coefficients[key] = value
            else:
                raise ValueError(f"Invalid reward coefficient: {key}")

    def plot_equity_history(self):
        plt.figure(figsize=(12, 6))
        plt.plot(self.equity_history)
        plt.xlabel("Timestep")
        plt.ylabel("Equity")
        plt.title("Equity History")
        plt.grid(True)
        plt.show()

    def summarize_done_trades(self):
        num_trades = len(self.done_trades)
        total_profit = sum(trade['trade_profit'] for trade in self.done_trades)
        average_profit = total_profit / num_trades if num_trades > 0 else 0

        winning_trades = [trade for trade in self.done_trades if trade['trade_profit'] > 0]
        losing_trades = [trade for trade in self.done_trades if trade['trade_profit'] <= 0]

        num_winning_trades = len(winning_trades)
        num_losing_trades = len(losing_trades)
        win_rate = num_winning_trades / num_trades if num_trades > 0 else 0

        summary = {
            'num_trades': num_trades,
            'total_profit': total_profit,
            'average_profit': average_profit,
            'win_rate': win_rate,
            'num_winning_trades': num_winning_trades,
            'num_losing_trades': num_losing_trades,
        }

        return summary

    def print_summary(self):
        print("\nAccount Information:")
        for key, value in self.account_info.items():
            if key in ['avg_price', 'spot_price']:
                print(f"{key.capitalize():<16}: {value:,.5f}")
            else:
                print(f"{key.capitalize():<16}: {value:,.2f}")
                
            if key in ['reward']:
                print(f"{key.capitalize():<16}: {value:,.6f}")
                
        print("\nPrevious Account Information:")
                
        for key, value in self.previous_account_info.items():
            if key in ['avg_price', 'spot_price']:
                print(f"{key.capitalize():<16}: {value:,.5f}")
            else:
                print(f"{key.capitalize():<16}: {value:,.2f}")

        print("\nOpen Positions:")
        for key, value in self.open_positions.items():
            if isinstance(value, float):
                if key in ['avg_price', 'spot_price']:
                    print(f"{key.capitalize():<16}: {value:,.5f}")
                else:
                    print(f"{key.capitalize():<16}: {value:,.2f}")
            else:
                print(f"{key.capitalize():<16}: {value}")

        print("\nDone Trades:")
        for trade in self.done_trades:
            trade_str = ", ".join(
                [f"{key.capitalize()}: {value:,.5f}" if key in ['price'] else f"{key.capitalize()}: {value:,.2f}" if isinstance(
                    value, (int, float)) else f"{key.capitalize()}: {value}" for key, value in trade.items()])
            print(f"{{{trade_str}}}")           

    def calculate_reward(self, weight_dense=0.8, weight_sparse=1, weight_shaping=0.3,
                        holding_reward=0.5, trading_cost_penalty=0.1, 
                        stop_out_penalty=20, realized_pnl_weight=2, 
                        invalid_action_penalty=0.0005, 
                        debug=False):
        
        current_equity = self.account_info["equity"]
        previous_equity = self.previous_account_info["equity"]
        initial_balance = self.initial_balance
        sortino_ratio = self.account_info["sortino_ratio"]
        stopped_out = self.stopped_out
        unrealized_pnl = self.account_info["unrealized_pnl"]
        nop = self.account_info["nop"]
        accumulated_cost = self.account_info["accumulated_cost"]
        realized_pnl = self.realized_pnl

        # Dense reward based on unrealized PnL
        dense_reward = unrealized_pnl * weight_dense

        # Holding reward for profitable positions
        holding_reward_value = holding_reward if nop != 0 and unrealized_pnl > 0 else 0

        # Trading cost penalty
        trading_cost_penalty_value = -min(accumulated_cost, trading_cost_penalty)

        # Shaping reward based on Sortino ratio
        shaping_reward = max(0, sortino_ratio) * weight_shaping

        # Penalty for invalid actions
        invalid_action_reward = -invalid_action_penalty if self.trade_not_executed else 0

        # Sparse reward based on log return
        log_return = np.log(current_equity / initial_balance) if current_equity > 0 and initial_balance > 0 else 0
        sparse_reward = log_return * weight_sparse

        # Realized PnL reward
        realized_pnl_reward = realized_pnl * realized_pnl_weight

        # Calculate total reward
        total_reward = dense_reward + holding_reward_value + trading_cost_penalty_value + shaping_reward + invalid_action_reward + realized_pnl_reward + sparse_reward

        if stopped_out:
            stop_out_reward = -stop_out_penalty
            total_reward += stop_out_reward

        # Debug information
        if debug:
            print(f"Current Equity: {current_equity:.2f}, Previous Equity: {previous_equity:.2f}, Initial Balance: {initial_balance:.2f}")
            print(f"Sortino Ratio: {sortino_ratio:.2f}, Dense Reward: {dense_reward:.6f}, Scaled Dense Reward: {dense_reward:.6f}")
            print(f"Holding Reward: {holding_reward_value:.6f}, Trading Cost Penalty: {trading_cost_penalty_value:.6f}")
            print(f"Scaled Shaping Reward: {shaping_reward:.6f}, Invalid Action Reward: {invalid_action_reward:.6f}")
            print(f"Realized PnL Reward: {realized_pnl_reward:.6f}, Sparse Reward (Log Return): {sparse_reward:.6f}")
            print(f"Total Reward: {total_reward:.6f}")

        return total_reward
