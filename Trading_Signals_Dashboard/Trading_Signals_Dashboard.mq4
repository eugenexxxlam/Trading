//+------------------------------------------------------------------+
//|                                           Ichimoku_Dashboard.mq4 |
//|                     Multi-Timeframe Ichimoku Indicator Dashboard |
//+------------------------------------------------------------------+
#property copyright "Eugene Lam"
#property link      ""
#property version   "2.40"
#property strict
#include <stdlib.mqh>
#include <stderror.mqh>

//+------------------------------------------------------------------+
//| Color Definitions for Trend Visualization                        |
//| Dark colors for individual timeframe signals                     |
//| Light colors for consensus/summary trend indicators              |
//+------------------------------------------------------------------+
#define BullColor C'18,82,38'
#define BearColor C'112,0,0'
#define LightBullColor C'67,213,112'
#define LightBearColor C'255,0,0'

//+------------------------------------------------------------------+
//| Signal State Constants                                            |
//| Standardized directional values used throughout the system       |
//+------------------------------------------------------------------+
#define NONE 0
#define DOWN -1
#define UP 1

//+------------------------------------------------------------------+
//| Dashboard Update Frequency Enumeration                           |
//+------------------------------------------------------------------+
enum dbu {Constant=0,OneMinute=1,FiveMinutes=5};

//+------------------------------------------------------------------+
//| Dashboard Configuration Parameters                               |
//| Controls symbol selection, display position, and update timing   |
//+------------------------------------------------------------------+
sinput   string   t_dashboard = "========== DASHBOARD SETTINGS ==========";
sinput bool       UseDefaultPairs = False;
sinput int        NumbersOfPairs = 1;
sinput string     Pair1 = "BTCUSD";
sinput dbu        DashUpdate = 0;
sinput int        x_axis = 0;
sinput int        y_axis = 20;

//+------------------------------------------------------------------+
//| Alert Configuration                                               |
//| Controls sound alerts and push notifications to mobile devices   |
//+------------------------------------------------------------------+
extern bool SoundAlert = true;
extern bool Sendnotification = true;

//+------------------------------------------------------------------+
//| Ichimoku Kinko Hyo Parameters                                    |
//| Standard periods: Tenkan-sen (9), Kijun-sen (26), Senkou (52)   |
//+------------------------------------------------------------------+
extern string Ichimoku_Settings = "---------- ICHIMOKU PARAMETERS ----------";
extern int tenkan = 9;
extern int kijun = 26;
extern int senkou = 52;

//+------------------------------------------------------------------+
//| Parabolic SAR Parameters                                         |
//| Step: increment factor, Maximum: acceleration limit              |
//+------------------------------------------------------------------+
extern string SAR_Settings = "------------ SAR PARAMETERS ------------";
extern double SAR_step = 0.02;
extern double SAR_maximum = 0.2;

//+------------------------------------------------------------------+
//| MACD Parameters                                                   |
//| Fast/Slow MA periods and signal line smoothing                   |
//+------------------------------------------------------------------+
extern string MACD_Settings = "----------- MACD PARAMETERS -----------";
extern int fastMA = 15;
extern int slowMA = 60;
extern int signal_period = 9;

//+------------------------------------------------------------------+
//| Average Directional Index (ADX) Configuration                    |
//| Period: calculation window, Strong trend: threshold level        |
//| Strict filter: require ADX > threshold for directional signals   |
//+------------------------------------------------------------------+
extern string ADX_Settings = "------------ ADX SETTINGS -------------";
extern int ADX_period = 14;
extern double ADX_strong_trend = 25;
extern bool ADX_strict_filter = true;

//+------------------------------------------------------------------+
//| Relative Strength Index (RSI) Configuration                      |
//| Threshold levels for bullish/bearish conditions                  |
//| Slope option: require directional momentum confirmation          |
//+------------------------------------------------------------------+
extern string RSI_Settings = "------------ RSI SETTINGS -------------";
extern int RSI_period = 14;
extern double RSI_bull_threshold = 55;
extern double RSI_bear_threshold = 45;
extern bool RSI_use_slope = true;

//+------------------------------------------------------------------+
//| Average True Range (ATR) Regime Detection Settings               |
//| Baseline periods: lookback for normal volatility calculation     |
//| Green ratio: expansion threshold for volatility breakout         |
//| Exit ratio: minimum level to maintain expansion state            |
//| Slope parameters: rate of change requirements for regime shift   |
//+------------------------------------------------------------------+
extern string ATR_Settings = "---------- ATR REGIME SETTINGS ----------";
extern int ATR_Period = 14;
extern int ATR_Baseline_M1 = 60;
extern int ATR_Baseline_M5 = 40;
extern int ATR_Baseline_M15 = 30;
extern int ATR_Baseline_H1 = 24;
extern int ATR_Baseline_H4 = 20;
extern int ATR_Baseline_D1 = 20;
extern int ATR_Slope_Lookback = 3;
extern double ATR_Green_Ratio = 1.20;
extern double ATR_Green_Exit = 1.10;
extern double ATR_Green_Slope_Pct = 0.02;
extern double ATR_Green_Stay_Slope = -0.04;

//+------------------------------------------------------------------+
//| Slope Analysis Thresholds                                        |
//| Bars: lookback period for slope calculation                      |
//| Threshold: minimum pip movement to register directional slope    |
//+------------------------------------------------------------------+
extern string Slope_Settings = "--------- SLOPE THRESHOLDS ---------";
extern int Tenkan_Slope_Bars = 3;
extern int Kijun_Slope_Bars = 5;
extern double Slope_Threshold_Pips = 2.0;

//+------------------------------------------------------------------+
//| Cloud Thickness Analysis                                         |
//| Ratio: multiple of ATR to qualify as thick/strong cloud          |
//+------------------------------------------------------------------+
extern string Cloud_Settings = "-------- CLOUD THICKNESS --------";
extern double Thick_Cloud_ATR_Ratio = 2.0;

//+------------------------------------------------------------------+
//| Dashboard Visual Appearance                                      |
//+------------------------------------------------------------------+
extern color BoarderColor = C'60,60,60';
extern color Box_Color = clrBlack;

//+------------------------------------------------------------------+
//| Alert Signal Type Filters                                        |
//| Four arrow types representing different breakout conditions      |
//| TC: Tenkan-sen crosses cloud boundary                           |
//| KC: Kijun-sen crosses cloud boundary                            |
//| Twist: Future cloud color change (Kumo twist)                   |
//| MACD: MACD line crosses signal line                             |
//+------------------------------------------------------------------+
extern string Alert_Arrow_Types = "===== ARROW TYPE ON/OFF =====";
extern bool Alert_TC_Arrow = TRUE;
extern bool Alert_KC_Arrow = TRUE;
extern bool Alert_Twist_Arrow = TRUE;
extern bool Alert_MACD_Arrow = FALSE;

//+------------------------------------------------------------------+
//| Alert Timeframe Filters                                          |
//| Enable/disable alerts for specific timeframes                    |
//| Higher timeframes generally produce more reliable signals        |
//+------------------------------------------------------------------+
extern string Alert_Timeframes = "===== TIMEFRAME ON/OFF =====";
extern bool Alert_M1 = FALSE;
extern bool Alert_M5 = TRUE;
extern bool Alert_M15 = TRUE;
extern bool Alert_H1 = TRUE;
extern bool Alert_H4 = TRUE;
extern bool Alert_D1 = TRUE;
extern bool Alert_W1 = TRUE;
extern bool Alert_MN = TRUE;

//+------------------------------------------------------------------+
//| Multi-Tier Confirmation System                                   |
//| Tier1: requires alignment of 10 core indicators on higher TFs    |
//| Allow_NONE: treat neutral indicators as non-conflicting          |
//+------------------------------------------------------------------+
extern string Alert_Filter_Settings = "========== ALERT FILTERING ==========";
extern int Tier1_Min_Match = 10;
extern bool Allow_NONE_As_Match = true;

//+------------------------------------------------------------------+
//| Probability Calculation Parameters                               |
//| Beta: controls sigmoid steepness for edge-to-probability mapping |
//| Smoothing: exponential moving average factor to reduce noise     |
//+------------------------------------------------------------------+
extern string Prob_Settings = "========== PROBABILITY SETTINGS ==========";
extern double Beta_Calibration = 3.0;
extern double Smoothing_Factor = 0.30;

//+------------------------------------------------------------------+
//| Asymmetric ATR Percentage Filter                                 |
//| Implements dynamic signal filtering based on daily range usage   |
//| Hunt Zone: optimal entry conditions (low ATR% usage)             |
//| Caution Zone: gradual signal reduction as range expands          |
//| Exhaustion Zone: suppress trend-following, boost counter-trend   |
//| This filter prevents chasing extended moves and favors reversals |
//+------------------------------------------------------------------+
extern string ATR_Filter_Settings_Prob = "========== ATR% FILTER (ASYMMETRIC) ==========";
extern bool   Use_ATR_Percent_Filter = true;
extern double ATR_Hunt_Zone_Max = 20.0;
extern double ATR_Caution_Zone = 85.0;
extern double ATR_Full_Suppress = 120.0;
extern bool   Allow_CounterTrend_Above_75 = true;
extern double CounterTrend_Boost = 1.2;

//+------------------------------------------------------------------+
//| Alert System Testing Variables                                   |
//+------------------------------------------------------------------+
datetime last_dummy_alert = 0;
int dummy_alert_counter = 0;

//+------------------------------------------------------------------+
//| Global Data Arrays                                                |
//| DefaultPairs: predefined symbol list for multi-asset monitoring  |
//| TradePairs: active symbols being analyzed                        |
//| ATR_Values: calculated ATR in pips for each symbol/timeframe     |
//+------------------------------------------------------------------+
string DefaultPairs[] = {"EURUSD","GBPUSD","USDJPY","AUDUSD","NZDUSD","USDCAD","USDCHF","FTSE100","GER30","JPN225","HK50","US30","NAS100","SPX500","USOIL","UKOIL","XAUUSD"};
string TradePairs[];
double ATR_Values[][8];

//+------------------------------------------------------------------+
//| Technical Indicator Signal Arrays (23 indicators)                |
//| Structure: [Symbol Index][Timeframe Index]                       |
//| Timeframes: 0=M1, 1=M5, 2=M15, 3=H1, 4=H4, 5=D1, 6=W1, 7=MN     |
//| Indices 8-10: Short/Medium/Long term consensus aggregations      |
//| Values: UP(1), DOWN(-1), NONE(0)                                 |
//+------------------------------------------------------------------+
int ATR[][11], SAR[][11], MACD[][11], RSI[][11], ADX[][11];
int PT[][11], TSlope[][11], TK[][11], TKDelta[][11], PK[][11], KSlope[][11];
int ChP[][11], PA[][11], PC[][11], PB[][11], TC[][11], KC[][11];
int Kumo[][11], Thick[][11], DeltaThick[][11], ChC[][11], ChFree[][11], Twist[][11];

//+------------------------------------------------------------------+
//| Arrow Signal Detection Arrays (4 signal types)                   |
//| TC_Arrow: Tenkan-sen breakout through cloud                     |
//| KC_Arrow: Kijun-sen breakout through cloud                      |
//| Twist_Arrow: Future cloud color change detection                |
//| MACD_Arrow: MACD line crosses signal line                       |
//+------------------------------------------------------------------+
int TC_Arrow[][11];
int KC_Arrow[][11];
int Twist_Arrow[][11];
int MACD_Arrow[][11];

//+------------------------------------------------------------------+
//| Color State Arrays for Visual Rendering                          |
//| Parallel arrays storing display colors for each indicator        |
//| Updated based on signal direction and consensus strength         |
//+------------------------------------------------------------------+
int ATR_color[][11], SAR_color[][11], MACD_color[][11], RSI_color[][11], ADX_color[][11];
int PT_color[][11], TSlope_color[][11], TK_color[][11], TKDelta_color[][11], PK_color[][11];
int KSlope_color[][11], ChP_color[][11], PA_color[][11], PC_color[][11], PB_color[][11];
int TC_color[][11], KC_color[][11], Kumo_color[][11], Thick_color[][11], DeltaThick_color[][11];
int ChC_color[][11], ChFree_color[][11], Twist_color[][11];

//+------------------------------------------------------------------+
//| ATR Regime State Memory                                          |
//| Maintains previous volatility regime to implement hysteresis     |
//| Prevents rapid regime switching due to noise                     |
//+------------------------------------------------------------------+
int Previous_ATR_Regime[][11];

//+------------------------------------------------------------------+
//| Alert State Management Arrays                                    |
//| Tracks current and previous arrow states to detect new signals   |
//| Prevents duplicate alerts for same condition                     |
//| Static arrays maintain state between function calls              |
//+------------------------------------------------------------------+
static bool is_TC_up[][11], is_TC_down[][11];
static bool is_KC_up[][11], is_KC_down[][11];
static bool is_Twist_up[][11], is_Twist_down[][11];
static bool is_MACD_up[][11], is_MACD_down[][11];

bool was_TC_up[][11], was_TC_down[][11];
bool was_KC_up[][11], was_KC_down[][11];
bool was_Twist_up[][11], was_Twist_down[][11];
bool was_MACD_up[][11], was_MACD_down[][11];

bool Is_Alert[][11], Condition_check[][11];

//+------------------------------------------------------------------+
//| Timeframe Summary Variables                                      |
//| Individual timeframe consensus signals (all 5 core indicators)   |
//| Short/Medium/Long: aggregated trend direction across TF groups   |
//+------------------------------------------------------------------+
int m1_box, m5_box, m15_box, h1_box, h4_box, d1_box, w1_box, mn_box;
int ShortTerm_trend, MediumTerm_trend, LongTerm_trend;

//+------------------------------------------------------------------+
//| Price Ladder and Display Variables                               |
//| reference_price: sorted array of key price levels                |
//| PipsColor/Prob_Color: dynamic colors based on value direction    |
//+------------------------------------------------------------------+
double reference_price[25];
color PipsColor, Prob_Color;

//+------------------------------------------------------------------+
//| Symbol Information Structure                                     |
//| Stores calculated metrics for each trading pair                  |
//| PairPip/pipsfactor: conversion values for pip calculations       |
//| Trading_Prob: quantitative probability score in range [-1, +1]   |
//+------------------------------------------------------------------+
struct pairinf
{
   double PairPip;
   int    pipsfactor;
   double Pips;
   double Spread;
   double dayHigh;
   double dayLow;
   double Price_Quote;
   double PercentageChange;
   double atr;
   double Trading_Prob;
};
pairinf pairinfo[];

//+------------------------------------------------------------------+
//| Price Level Structure for Sorted Display                         |
//| Contains price value, originating timeframe, and indicator type  |
//| Used to create vertical price ladder with resistance/support     |
//| Type codes: 0=Kijun, 1=SpanA, 2=SpanB, 3=CurrentPrice           |
//+------------------------------------------------------------------+
struct PriceLevelInfo
{
   double price;
   int    timeframe;
   int    type;
};

PriceLevelInfo price_levels[25];

datetime newday = 0;

//+------------------------------------------------------------------+
//| Get Trading Pair Array Index                                     |
//| Returns: array index of specified pair, -1 if not found          |
//+------------------------------------------------------------------+
int GetPairIndex(string pair)
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
      if(TradePairs[i] == pair) return i;
   return -1;
}

//+------------------------------------------------------------------+
//| Get ATR Baseline Calculation Period                              |
//| Returns timeframe-specific lookback period for ATR baseline      |
//| Shorter timeframes use longer baselines for stability            |
//+------------------------------------------------------------------+
int GetATRBaselinePeriod(int timeframe)
{
   switch(timeframe)
   {
      case PERIOD_M1:  return ATR_Baseline_M1;
      case PERIOD_M5:  return ATR_Baseline_M5;
      case PERIOD_M15: return ATR_Baseline_M15;
      case PERIOD_H1:  return ATR_Baseline_H1;
      case PERIOD_H4:  return ATR_Baseline_H4;
      case PERIOD_D1:  return ATR_Baseline_D1;
      case PERIOD_W1:  return 20;
      case PERIOD_MN1: return 12;
      default:         return 20;
   }
}

//+------------------------------------------------------------------+
//| Convert Array Index to MT4 Timeframe Constant                    |
//| Maps internal 0-7 indexing to MT4 period constants               |
//+------------------------------------------------------------------+
int NumberToTimeframe(int number)
{
   switch(number)
   {
      case 0: return 1;
      case 1: return 5;
      case 2: return 15;
      case 3: return 60;
      case 4: return 240;
      case 5: return 1440;
      case 6: return 10080;
      case 7: return 43200;
      default: return 1;
   }
}

//+------------------------------------------------------------------+
//| Get Abbreviated Timeframe Name for Display                       |
//| Returns: short string representation of timeframe                |
//+------------------------------------------------------------------+
string GetTFShortName(int tf_index)
{
   switch(tf_index)
   {
      case 0: return "M1";
      case 1: return "M5";
      case 2: return "15";
      case 3: return "H1";
      case 4: return "H4";
      case 5: return "D1";
      case 6: return "W1";
      case 7: return "MN";
      default: return "??";
   }
}

//+------------------------------------------------------------------+
//| Get Price Level Type Indicator                                   |
//| Returns: single-character code for price ladder display          |
//+------------------------------------------------------------------+
string GetTypeShortName(int type_index)
{
   switch(type_index)
   {
      case 0: return "K";
      case 1: return "A";
      case 2: return "B";
      case 3: return "**";
      default: return "?";
   }
}

//+------------------------------------------------------------------+
//| Sort Price Levels in Descending Order                            |
//| Implementation: bubble sort algorithm                            |
//| Organizes 25 price levels from highest to lowest                 |
//+------------------------------------------------------------------+
void SortPriceLevels()
{
   for(int i = 0; i < 24; i++)
   {
      for(int j = 0; j < 24 - i; j++)
      {
         if(price_levels[j].price < price_levels[j + 1].price)
         {
            PriceLevelInfo temp;
            temp.price = price_levels[j].price;
            temp.timeframe = price_levels[j].timeframe;
            temp.type = price_levels[j].type;
            
            price_levels[j].price = price_levels[j + 1].price;
            price_levels[j].timeframe = price_levels[j + 1].timeframe;
            price_levels[j].type = price_levels[j + 1].type;
            
            price_levels[j + 1].price = temp.price;
            price_levels[j + 1].timeframe = temp.timeframe;
            price_levels[j + 1].type = temp.type;
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Determine Today's Price Movement Direction                       |
//| Compares daily close vs open with noise threshold                |
//| Returns: UP, DOWN, or NONE                                       |
//+------------------------------------------------------------------+
int GetDayDirection(int pair_idx)
{
   double day_open = iOpen(TradePairs[pair_idx], PERIOD_D1, 0);
   double day_close = iClose(TradePairs[pair_idx], PERIOD_D1, 0);
   
   double threshold = pairinfo[pair_idx].PairPip * 10;
   
   if(day_close > day_open + threshold) return UP;
   if(day_close < day_open - threshold) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Calculate Today's Price Movement in Pips                         |
//| Returns: signed pip value (positive=up, negative=down)           |
//+------------------------------------------------------------------+
double GetDayMovePips(int pair_idx)
{
   double day_open = iOpen(TradePairs[pair_idx], PERIOD_D1, 0);
   double day_close = iClose(TradePairs[pair_idx], PERIOD_D1, 0);
   
   if(pairinfo[pair_idx].PairPip <= 0 || pairinfo[pair_idx].pipsfactor <= 0) 
      return 0;
   
   return (day_close - day_open) / pairinfo[pair_idx].PairPip / pairinfo[pair_idx].pipsfactor;
}

//+------------------------------------------------------------------+
//| Calculate MACD Momentum Signal                                   |
//| Compares current vs previous MACD main line value                |
//| Returns: UP if rising, DOWN if falling, NONE if flat             |
//+------------------------------------------------------------------+
int Get_MACD_Signals(string tradepair, int timeframe, int candel)
{
   double current_MACD = iMACD(tradepair, timeframe, fastMA, slowMA, 9, 0, 0, candel);
   double previous_MACD = iMACD(tradepair, timeframe, fastMA, slowMA, 9, 0, 0, candel+1);
   if(current_MACD > previous_MACD) return UP;
   if(current_MACD < previous_MACD) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Calculate Parabolic SAR Signal                                   |
//| Compares SAR position relative to price                          |
//| Returns: UP if SAR below price, DOWN if above                    |
//+------------------------------------------------------------------+
int Get_SAR_Signals(string tradepair, int timeframe, int candle)
{
   double close_price = iClose(tradepair, timeframe, candle);
   double SAR_current = iSAR(tradepair, timeframe, SAR_step, SAR_maximum, candle);
   if(SAR_current < close_price) return UP;
   if(SAR_current > close_price) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Calculate RSI Momentum Signal                                    |
//| Option 1 (slope enabled): requires level + directional momentum  |
//| Option 2 (slope disabled): uses threshold levels only            |
//| Returns: UP if bullish conditions met, DOWN if bearish           |
//+------------------------------------------------------------------+
int Get_RSI_Signals(string tradepair, int timeframe, int candle)
{
   double RSI_current = iRSI(tradepair, timeframe, RSI_period, PRICE_CLOSE, candle);
   double RSI_previous = iRSI(tradepair, timeframe, RSI_period, PRICE_CLOSE, candle + 1);
   double RSI_momentum = RSI_current - RSI_previous;
   
   if(RSI_use_slope)
   {
      if(RSI_current > RSI_bull_threshold && RSI_momentum > 0) return UP;
      if(RSI_current < RSI_bear_threshold && RSI_momentum < 0) return DOWN;
   }
   else
   {
      if(RSI_current > RSI_bull_threshold) return UP;
      if(RSI_current < RSI_bear_threshold) return DOWN;
   }
   return NONE;
}

//+------------------------------------------------------------------+
//| Calculate ADX Directional Signal                                 |
//| Strict mode: requires ADX > threshold for signal validity        |
//| Non-strict: uses DI comparison regardless of ADX level           |
//| Returns: UP if +DI dominant, DOWN if -DI dominant                |
//+------------------------------------------------------------------+
int Get_ADX_Signals(string tradepair, int timeframe, int candle)
{
   double ADX_main = iADX(tradepair, timeframe, ADX_period, PRICE_CLOSE, MODE_MAIN, candle);
   double ADX_plusDI = iADX(tradepair, timeframe, ADX_period, PRICE_CLOSE, MODE_PLUSDI, candle);
   double ADX_minusDI = iADX(tradepair, timeframe, ADX_period, PRICE_CLOSE, MODE_MINUSDI, candle);
   
   if(ADX_strict_filter)
   {
      if(ADX_main > ADX_strong_trend)
      {
         if(ADX_plusDI > ADX_minusDI) return UP;
         if(ADX_minusDI > ADX_plusDI) return DOWN;
      }
   }
   else
   {
      if(ADX_plusDI > ADX_minusDI) return UP;
      if(ADX_minusDI > ADX_plusDI) return DOWN;
   }
   return NONE;
}

//+------------------------------------------------------------------+
//| Calculate ATR Volatility Regime Signal                           |
//| Compares current ATR against rolling baseline average            |
//| Entry condition: ratio exceeds threshold with positive slope     |
//| Exit condition: ratio falls below exit threshold or slope drops  |
//| Implements hysteresis to prevent rapid state oscillation         |
//| Returns: UP if volatility expansion detected, NONE otherwise     |
//+------------------------------------------------------------------+
int Get_ATR_Signals(string tradepair, int timeframe, int candel)
{
   int pairIndex = GetPairIndex(tradepair);
   if(pairIndex == -1) return NONE;
   
   int tfIndex = -1;
   switch(timeframe)
   {
      case PERIOD_M1:  tfIndex = 0; break;
      case PERIOD_M5:  tfIndex = 1; break;
      case PERIOD_M15: tfIndex = 2; break;
      case PERIOD_H1:  tfIndex = 3; break;
      case PERIOD_H4:  tfIndex = 4; break;
      case PERIOD_D1:  tfIndex = 5; break;
      case PERIOD_W1:  tfIndex = 6; break;
      case PERIOD_MN1: tfIndex = 7; break;
      default: return NONE;
   }
   
   int bar_shift = (candel == 0) ? 1 : candel;
   double atr_now = iATR(tradepair, timeframe, ATR_Period, bar_shift);
   if(atr_now <= 0) return NONE;
   
   int baseline_period = GetATRBaselinePeriod(timeframe);
   double atr_sum = 0;
   int baseline_count = 0;
   
   for(int i = bar_shift + 1; i <= bar_shift + baseline_period; i++)
   {
      double atr_bar = iATR(tradepair, timeframe, ATR_Period, i);
      if(atr_bar > 0)
      {
         atr_sum += atr_bar;
         baseline_count++;
      }
   }
   
   if(baseline_count == 0) return NONE;
   double atr_baseline = atr_sum / baseline_count;
   if(atr_baseline <= 0) return NONE;
   
   double atr_ratio = atr_now / atr_baseline;
   double atr_old = iATR(tradepair, timeframe, ATR_Period, bar_shift + ATR_Slope_Lookback);
   if(atr_old <= 0) return NONE;
   
   double atr_slope_pct = (atr_now - atr_old) / atr_old;
   
   int previous_regime = Previous_ATR_Regime[pairIndex][tfIndex];
   int new_regime = NONE;
   
   if(atr_ratio >= ATR_Green_Ratio && atr_slope_pct > ATR_Green_Slope_Pct)
      new_regime = UP;
   else if(previous_regime == UP && atr_ratio >= ATR_Green_Exit && atr_slope_pct > ATR_Green_Stay_Slope)
      new_regime = UP;
   
   Previous_ATR_Regime[pairIndex][tfIndex] = new_regime;
   return new_regime;
}

//+------------------------------------------------------------------+
//| Price vs Tenkan-sen Signal                                       |
//| Returns: UP if price above Tenkan, DOWN if below                 |
//+------------------------------------------------------------------+
int Get_PT_Signals(string pair, int tf, int bar)
{
   double price = iClose(pair, tf, bar);
   double tenkan_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar);
   if(price > tenkan_val) return UP;
   if(price < tenkan_val) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Tenkan-sen Slope Direction                                       |
//| Calculates pip change over specified lookback period             |
//| Returns: UP if slope exceeds threshold, DOWN if below threshold  |
//+------------------------------------------------------------------+
int Get_TSlope_Signals(string pair, int tf, int bar)
{
   int pairIdx = GetPairIndex(pair);
   if(pairIdx == -1) return NONE;
   
   double tenkan_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar);
   double tenkan_old = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar + Tenkan_Slope_Bars);
   double slope_pips = (tenkan_now - tenkan_old) / pairinfo[pairIdx].PairPip / pairinfo[pairIdx].pipsfactor;
   
   if(slope_pips > Slope_Threshold_Pips) return UP;
   if(slope_pips < -Slope_Threshold_Pips) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Tenkan vs Kijun Cross Signal                                     |
//| Classic Ichimoku TK cross for trend confirmation                 |
//| Returns: UP if Tenkan above Kijun, DOWN if below                 |
//+------------------------------------------------------------------+
int Get_TK_Signals(string pair, int tf, int bar)
{
   double tenkan_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar);
   double kijun_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar);
   if(tenkan_val > kijun_val) return UP;
   if(tenkan_val < kijun_val) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Tenkan-Kijun Separation Distance                                 |
//| Measures TK spread relative to ATR for trend strength            |
//| Returns: UP if distance exceeds ATR threshold                    |
//+------------------------------------------------------------------+
int Get_TKDelta_Signals(string pair, int tf, int bar)
{
   double tenkan_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar);
   double kijun_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar);
   double distance = MathAbs(tenkan_val - kijun_val);
   double atr = iATR(pair, tf, ATR_Period, bar);
   if(distance > atr) return UP;
   return NONE;
}

//+------------------------------------------------------------------+
//| Price vs Kijun-sen Signal                                        |
//| Primary trend direction indicator in Ichimoku system             |
//| Returns: UP if price above Kijun, DOWN if below                  |
//+------------------------------------------------------------------+
int Get_PK_Signals(string pair, int tf, int bar)
{
   double price = iClose(pair, tf, bar);
   double kijun_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar);
   if(price > kijun_val) return UP;
   if(price < kijun_val) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Kijun-sen Slope Direction                                        |
//| Slower moving baseline slope for trend persistence               |
//| Returns: UP if slope exceeds threshold, DOWN if below threshold  |
//+------------------------------------------------------------------+
int Get_KSlope_Signals(string pair, int tf, int bar)
{
   int pairIdx = GetPairIndex(pair);
   if(pairIdx == -1) return NONE;
   
   double kijun_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar);
   double kijun_old = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar + Kijun_Slope_Bars);
   double slope_pips = (kijun_now - kijun_old) / pairinfo[pairIdx].PairPip / pairinfo[pairIdx].pipsfactor;
   
   if(slope_pips > Slope_Threshold_Pips) return UP;
   if(slope_pips < -Slope_Threshold_Pips) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Chikou Span vs Historical Price Signal                           |
//| Compares current close to price 26 periods ago                   |
//| Returns: UP if above historical price, DOWN if below             |
//+------------------------------------------------------------------+
int Get_ChP_Signals(string pair, int tf, int bar)
{
   double current_close = iClose(pair, tf, bar);
   double close_26_ago = iClose(pair, tf, bar + kijun);
   if(current_close > close_26_ago) return UP;
   if(current_close < close_26_ago) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Price vs Senkou Span A Signal                                    |
//| Leading indicator component of cloud                             |
//| Returns: UP if price above Span A, DOWN if below                 |
//+------------------------------------------------------------------+
int Get_PA_Signals(string pair, int tf, int bar)
{
   double price = iClose(pair, tf, bar);
   double spanA = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   if(price > spanA) return UP;
   if(price < spanA) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Price vs Cloud (Kumo) Signal                                     |
//| Most important Ichimoku trend confirmation                       |
//| Cloud defined by Span A and Span B boundaries                    |
//| Returns: UP if price above cloud, DOWN if below cloud            |
//+------------------------------------------------------------------+
int Get_PC_Signals(string pair, int tf, int bar)
{
   double price = iClose(pair, tf, bar);
   double spanA = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   double cloudTop = MathMax(spanA, spanB);
   double cloudBottom = MathMin(spanA, spanB);
   
   if(price > cloudTop) return UP;
   if(price < cloudBottom) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Price vs Senkou Span B Signal                                    |
//| Slowest moving cloud component, strongest support/resistance     |
//| Returns: UP if price above Span B, DOWN if below                 |
//+------------------------------------------------------------------+
int Get_PB_Signals(string pair, int tf, int bar)
{
   double price = iClose(pair, tf, bar);
   double spanB = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   if(price > spanB) return UP;
   if(price < spanB) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Tenkan-sen vs Cloud Signal                                       |
//| Short-term line position relative to cloud                       |
//| Returns: UP if Tenkan above cloud, DOWN if below cloud           |
//+------------------------------------------------------------------+
int Get_TC_Signals(string pair, int tf, int bar)
{
   double tenkan_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar);
   double spanA = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   double cloudTop = MathMax(spanA, spanB);
   double cloudBottom = MathMin(spanA, spanB);
   
   if(tenkan_val > cloudTop) return UP;
   if(tenkan_val < cloudBottom) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Kijun-sen vs Cloud Signal                                        |
//| Medium-term line position relative to cloud                      |
//| Returns: UP if Kijun above cloud, DOWN if below cloud            |
//+------------------------------------------------------------------+
int Get_KC_Signals(string pair, int tf, int bar)
{
   double kijun_val = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar);
   double spanA = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   double cloudTop = MathMax(spanA, spanB);
   double cloudBottom = MathMin(spanA, spanB);
   
   if(kijun_val > cloudTop) return UP;
   if(kijun_val < cloudBottom) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Cloud Color (Kumo) Direction Signal                              |
//| Compares Senkou Span A vs Span B relationship                    |
//| Returns: UP if Span A > Span B (green cloud), DOWN if reversed   |
//+------------------------------------------------------------------+
int Get_Kumo_Signals(string pair, int tf, int bar)
{
   double spanA = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   if(spanA > spanB) return UP;
   if(spanB > spanA) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Cloud Thickness Signal                                           |
//| Measures cloud strength via Span A/B separation vs ATR           |
//| Thick cloud indicates strong support/resistance zone             |
//| Returns: UP if thickness exceeds ATR multiple threshold          |
//+------------------------------------------------------------------+
int Get_Thick_Signals(string pair, int tf, int bar)
{
   double spanA = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   double thickness = MathAbs(spanA - spanB);
   double atr = iATR(pair, tf, ATR_Period, bar);
   if(thickness > atr * Thick_Cloud_ATR_Ratio) return UP;
   return NONE;
}

//+------------------------------------------------------------------+
//| Cloud Thickness Change Signal                                    |
//| Monitors expansion or contraction of cloud over 5 bars           |
//| Returns: UP if expanding, DOWN if contracting                    |
//+------------------------------------------------------------------+
int Get_DeltaThick_Signals(string pair, int tf, int bar)
{
   double spanA_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   double thick_now = MathAbs(spanA_now - spanB_now);
   
   double spanA_old = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar + 5);
   double spanB_old = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar + 5);
   double thick_old = MathAbs(spanA_old - spanB_old);
   
   if(thick_now > thick_old * 1.1) return UP;
   if(thick_now < thick_old * 0.9) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Chikou Span vs Historical Cloud Signal                           |
//| Projects current price backward and compares to past cloud       |
//| Checks if Chikou would be above/below cloud 26 periods ago       |
//| Returns: UP if above historical cloud, DOWN if below             |
//+------------------------------------------------------------------+
int Get_ChC_Signals(string pair, int tf, int bar)
{
   double current_close = iClose(pair, tf, bar);
   int chikou_position = bar + kijun;
   
   double spanA_at_chikou_pos = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, chikou_position);
   double spanB_at_chikou_pos = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, chikou_position);
   double cloudTop = MathMax(spanA_at_chikou_pos, spanB_at_chikou_pos);
   double cloudBottom = MathMin(spanA_at_chikou_pos, spanB_at_chikou_pos);
   
   if(current_close > cloudTop) return UP;
   if(current_close < cloudBottom) return DOWN;
   return NONE;
}

//+------------------------------------------------------------------+
//| Chikou Span Free Space Signal                                    |
//| Counts price obstacles in Chikou's backward path                 |
//| Low obstacle count indicates clean trend potential               |
//| Returns: UP if obstacles <= 5 in 26-bar lookback                 |
//+------------------------------------------------------------------+
int Get_ChFree_Signals(string pair, int tf, int bar)
{
   double current_close = iClose(pair, tf, bar);
   int obstacles = 0;
   
   for(int i = bar + 1; i <= bar + kijun; i++)
   {
      double high = iHigh(pair, tf, i);
      double low = iLow(pair, tf, i);
      if(current_close >= low && current_close <= high) obstacles++;
   }
   
   if(obstacles <= 5) return UP;
   return NONE;
}

//+------------------------------------------------------------------+
//| Future Cloud Twist Signal                                        |
//| Analyzes projected cloud color 10 periods ahead                  |
//| Early warning of potential trend reversal                        |
//| Returns: UP if future cloud bullish, DOWN if bearish             |
//+------------------------------------------------------------------+
int Get_Twist_Signals(string pair, int tf, int bar)
{
   double spanA_future = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar - 10);
   double spanB_future = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar - 10);
   if(spanA_future > spanB_future) return UP;
   return DOWN;
}

//+------------------------------------------------------------------+
//| Tenkan Breakout Arrow Detection                                  |
//| Identifies when Tenkan-sen crosses cloud boundary                |
//| Compares previous bar vs current bar for breakout confirmation   |
//| Returns: UP for bullish breakout, DOWN for bearish breakdown     |
//+------------------------------------------------------------------+
int Get_TC_Arrow_Signals(string pair, int tf, int bar)
{
   double tenkan_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar);
   double tenkan_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_TENKANSEN, bar + 1);
   
   double spanA_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   double spanA_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar + 1);
   double spanB_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar + 1);
   
   double cloudTop_now = MathMax(spanA_now, spanB_now);
   double cloudBottom_now = MathMin(spanA_now, spanB_now);
   double cloudTop_prev = MathMax(spanA_prev, spanB_prev);
   double cloudBottom_prev = MathMin(spanA_prev, spanB_prev);
   
   if(tenkan_prev <= cloudTop_prev && tenkan_now > cloudTop_now) return UP;
   if(tenkan_prev >= cloudBottom_prev && tenkan_now < cloudBottom_now) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Kijun Breakout Arrow Detection                                   |
//| Identifies when Kijun-sen crosses cloud boundary                 |
//| More significant than Tenkan breakout due to slower period       |
//| Returns: UP for bullish breakout, DOWN for bearish breakdown     |
//+------------------------------------------------------------------+
int Get_KC_Arrow_Signals(string pair, int tf, int bar)
{
   double kijun_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar);
   double kijun_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_KIJUNSEN, bar + 1);
   
   double spanA_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar);
   double spanB_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar);
   double spanA_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar + 1);
   double spanB_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar + 1);
   
   double cloudTop_now = MathMax(spanA_now, spanB_now);
   double cloudBottom_now = MathMin(spanA_now, spanB_now);
   double cloudTop_prev = MathMax(spanA_prev, spanB_prev);
   double cloudBottom_prev = MathMin(spanA_prev, spanB_prev);
   
   if(kijun_prev <= cloudTop_prev && kijun_now > cloudTop_now) return UP;
   if(kijun_prev >= cloudBottom_prev && kijun_now < cloudBottom_now) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Future Cloud Twist Arrow Detection                               |
//| Detects actual color change in projected cloud                   |
//| Examines Span A/B relationship at future projection point        |
//| Returns: UP when cloud changes to green, DOWN when to red        |
//+------------------------------------------------------------------+
int Get_Twist_Arrow_Signals(string pair, int tf, int bar)
{
   double spanA_future_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar - kijun);
   double spanB_future_now = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar - kijun);
   double spanA_future_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANA, bar - kijun + 1);
   double spanB_future_prev = iIchimoku(pair, tf, tenkan, kijun, senkou, MODE_SENKOUSPANB, bar - kijun + 1);
   
   bool was_red = (spanB_future_prev > spanA_future_prev);
   bool now_green = (spanA_future_now > spanB_future_now);
   if(was_red && now_green) return UP;
   
   bool was_green = (spanA_future_prev > spanB_future_prev);
   bool now_red = (spanB_future_now > spanA_future_now);
   if(was_green && now_red) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| MACD Crossover Arrow Detection                                   |
//| Identifies MACD line crossing signal line in trending territory  |
//| Requires both lines above/below zero for directional bias        |
//| Returns: UP for bullish cross above zero, DOWN for bearish below |
//+------------------------------------------------------------------+
int Get_MACD_Arrow_Signals(string pair, int tf, int bar)
{
   double macd_main_now = iMACD(pair, tf, fastMA, slowMA, signal_period, PRICE_CLOSE, MODE_MAIN, bar);
   double macd_sig_now = iMACD(pair, tf, fastMA, slowMA, signal_period, PRICE_CLOSE, MODE_SIGNAL, bar);
   double macd_main_prev = iMACD(pair, tf, fastMA, slowMA, signal_period, PRICE_CLOSE, MODE_MAIN, bar + 1);
   double macd_sig_prev = iMACD(pair, tf, fastMA, slowMA, signal_period, PRICE_CLOSE, MODE_SIGNAL, bar + 1);
   
   if(macd_main_now > 0 && macd_sig_now > 0 && macd_main_prev > 0 && macd_sig_prev > 0 &&
      macd_main_now > macd_sig_now && macd_main_prev <= macd_sig_prev) return UP;
   
   if(macd_main_now < 0 && macd_sig_now < 0 && macd_main_prev < 0 && macd_sig_prev < 0 &&
      macd_main_now < macd_sig_now && macd_main_prev >= macd_sig_prev) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Create or Update Text Label on Chart                             |
//| Parameters: object name, display text, coordinates, color, size  |
//+------------------------------------------------------------------+
void SetText(string name, string text, int x, int y, color colour, int fontsize=12)
{
   if(ObjectFind(0, name) < 0) ObjectCreate(0, name, OBJ_LABEL, 0, 0, 0);
   ObjectSetInteger(0, name, OBJPROP_XDISTANCE, x);
   ObjectSetInteger(0, name, OBJPROP_YDISTANCE, y);
   ObjectSetInteger(0, name, OBJPROP_COLOR, colour);
   ObjectSetInteger(0, name, OBJPROP_FONTSIZE, fontsize);
   ObjectSetInteger(0, name, OBJPROP_CORNER, CORNER_LEFT_UPPER);
   ObjectSetString(0, name, OBJPROP_TEXT, text);
}

//+------------------------------------------------------------------+
//| Create Rectangular Panel for Dashboard Elements                  |
//| Configures position, dimensions, colors, and border properties   |
//| All panels anchored to upper-left corner for consistent layout   |
//+------------------------------------------------------------------+
void SetPanel(string name, int sub_window, int x, int y, int width, int height, color bg_color, color border_clr, int border_width)
{
   if(ObjectCreate(0, name, OBJ_RECTANGLE_LABEL, sub_window, 0, 0))
   {
      ObjectSetInteger(0, name, OBJPROP_XDISTANCE, x);
      ObjectSetInteger(0, name, OBJPROP_YDISTANCE, y);
      ObjectSetInteger(0, name, OBJPROP_XSIZE, width);
      ObjectSetInteger(0, name, OBJPROP_YSIZE, height);
      ObjectSetInteger(0, name, OBJPROP_COLOR, border_clr);
      ObjectSetInteger(0, name, OBJPROP_BORDER_TYPE, BORDER_FLAT);
      ObjectSetInteger(0, name, OBJPROP_WIDTH, border_width);
      ObjectSetInteger(0, name, OBJPROP_CORNER, CORNER_LEFT_UPPER);
      ObjectSetInteger(0, name, OBJPROP_STYLE, STYLE_SOLID);
      ObjectSetInteger(0, name, OBJPROP_BACK, true);
      ObjectSetInteger(0, name, OBJPROP_SELECTABLE, 0);
      ObjectSetInteger(0, name, OBJPROP_SELECTED, 0);
      ObjectSetInteger(0, name, OBJPROP_HIDDEN, true);
      ObjectSetInteger(0, name, OBJPROP_ZORDER, 0);
   }
   ObjectSetInteger(0, name, OBJPROP_BGCOLOR, bg_color);
}

//+------------------------------------------------------------------+
//| Update Panel Colors Dynamically                                  |
//| Modifies existing panel without recreating object                |
//+------------------------------------------------------------------+
void ColorPanel(string name, color bg_color, color border_clr)
{
   ObjectSetInteger(0, name, OBJPROP_COLOR, border_clr);
   ObjectSetInteger(0, name, OBJPROP_BGCOLOR, bg_color);
}

//+------------------------------------------------------------------+
//| Create Wingdings Symbol Text Object                              |
//| Used for arrow indicators in dashboard display                   |
//| Font: Wingdings provides arrow characters (233=up, 234=down)     |
//+------------------------------------------------------------------+
void SetObjText(string name, string CharToStr, int x, int y, color colour, int fontsize=12)
{
   if(ObjectFind(0, name) < 0) ObjectCreate(0, name, OBJ_LABEL, 0, 0, 0);
   ObjectSetInteger(0, name, OBJPROP_FONTSIZE, fontsize);
   ObjectSetInteger(0, name, OBJPROP_COLOR, colour);
   ObjectSetInteger(0, name, OBJPROP_BACK, false);
   ObjectSetInteger(0, name, OBJPROP_CORNER, CORNER_LEFT_UPPER);
   ObjectSetInteger(0, name, OBJPROP_XDISTANCE, x);
   ObjectSetInteger(0, name, OBJPROP_YDISTANCE, y);
   ObjectSetString(0, name, OBJPROP_TEXT, CharToStr);
   ObjectSetString(0, name, OBJPROP_FONT, "Wingdings");
}

//+------------------------------------------------------------------+
//| Expert Advisor Initialization Function                           |
//| Executed once when EA is loaded onto chart                       |
//| Responsibilities:                                                |
//| 1. Initialize trading pair list (default or custom)              |
//| 2. Allocate all dynamic arrays based on number of pairs          |
//| 3. Calculate pip values and factors for each symbol              |
//| 4. Build complete dashboard UI structure                         |
//| 5. Initialize alert state tracking arrays                        |
//| 6. Start timer for periodic updates                              |
//+------------------------------------------------------------------+
int OnInit()
{
   if(UseDefaultPairs) ArrayCopy(TradePairs, DefaultPairs);
   else
   {
      ArrayResize(TradePairs, NumbersOfPairs);
      TradePairs[0] = Pair1;
   }
   
   if(ArraySize(TradePairs) <= 0) return(INIT_FAILED);
   
   int numPairs = ArraySize(TradePairs);
   
   ArrayResize(pairinfo, numPairs);
   ArrayResize(ATR_Values, numPairs);
   ArrayResize(ATR, numPairs); ArrayResize(SAR, numPairs); ArrayResize(MACD, numPairs); ArrayResize(RSI, numPairs); ArrayResize(ADX, numPairs);
   ArrayResize(PT, numPairs); ArrayResize(TSlope, numPairs); ArrayResize(TK, numPairs); ArrayResize(TKDelta, numPairs); ArrayResize(PK, numPairs);
   ArrayResize(KSlope, numPairs); ArrayResize(ChP, numPairs); ArrayResize(PA, numPairs); ArrayResize(PC, numPairs); ArrayResize(PB, numPairs);
   ArrayResize(TC, numPairs); ArrayResize(KC, numPairs); ArrayResize(Kumo, numPairs); ArrayResize(Thick, numPairs); ArrayResize(DeltaThick, numPairs);
   ArrayResize(ChC, numPairs); ArrayResize(ChFree, numPairs); ArrayResize(Twist, numPairs);
   
   ArrayResize(TC_Arrow, numPairs);
   ArrayResize(KC_Arrow, numPairs);
   ArrayResize(Twist_Arrow, numPairs);
   ArrayResize(MACD_Arrow, numPairs);
   
   ArrayResize(ATR_color, numPairs); ArrayResize(SAR_color, numPairs); ArrayResize(MACD_color, numPairs); ArrayResize(RSI_color, numPairs); ArrayResize(ADX_color, numPairs);
   ArrayResize(PT_color, numPairs); ArrayResize(TSlope_color, numPairs); ArrayResize(TK_color, numPairs); ArrayResize(TKDelta_color, numPairs); ArrayResize(PK_color, numPairs);
   ArrayResize(KSlope_color, numPairs); ArrayResize(ChP_color, numPairs); ArrayResize(PA_color, numPairs); ArrayResize(PC_color, numPairs); ArrayResize(PB_color, numPairs);
   ArrayResize(TC_color, numPairs); ArrayResize(KC_color, numPairs); ArrayResize(Kumo_color, numPairs); ArrayResize(Thick_color, numPairs); ArrayResize(DeltaThick_color, numPairs);
   ArrayResize(ChC_color, numPairs); ArrayResize(ChFree_color, numPairs); ArrayResize(Twist_color, numPairs);
   
   ArrayResize(is_TC_up, numPairs); ArrayResize(is_TC_down, numPairs);
   ArrayResize(is_KC_up, numPairs); ArrayResize(is_KC_down, numPairs);
   ArrayResize(is_Twist_up, numPairs); ArrayResize(is_Twist_down, numPairs);
   ArrayResize(is_MACD_up, numPairs); ArrayResize(is_MACD_down, numPairs);
   ArrayResize(was_TC_up, numPairs); ArrayResize(was_TC_down, numPairs);
   ArrayResize(was_KC_up, numPairs); ArrayResize(was_KC_down, numPairs);
   ArrayResize(was_Twist_up, numPairs); ArrayResize(was_Twist_down, numPairs);
   ArrayResize(was_MACD_up, numPairs); ArrayResize(was_MACD_down, numPairs);
   ArrayResize(Is_Alert, numPairs); ArrayResize(Condition_check, numPairs); ArrayResize(Previous_ATR_Regime, numPairs);
   
   for(int i = 0; i < numPairs; i++)
   {
      for(int j = 0; j < 11; j++)
      {
         Previous_ATR_Regime[i][j] = NONE;
         is_TC_up[i][j] = false; is_TC_down[i][j] = false;
         is_KC_up[i][j] = false; is_KC_down[i][j] = false;
         is_Twist_up[i][j] = false; is_Twist_down[i][j] = false;
         is_MACD_up[i][j] = false; is_MACD_down[i][j] = false;
         was_TC_up[i][j] = false; was_TC_down[i][j] = false;
         was_KC_up[i][j] = false; was_KC_down[i][j] = false;
         was_Twist_up[i][j] = false; was_Twist_down[i][j] = false;
         was_MACD_up[i][j] = false; was_MACD_down[i][j] = false;
      }
      
      if(MarketInfo(TradePairs[i], MODE_DIGITS) == 5 || MarketInfo(TradePairs[i], MODE_DIGITS) == 3)
      {
         pairinfo[i].PairPip = MarketInfo(TradePairs[i], MODE_POINT);
         pairinfo[i].pipsfactor = 1;
      }
      else
      {
         pairinfo[i].PairPip = MarketInfo(TradePairs[i], MODE_POINT) * 10;
         pairinfo[i].pipsfactor = 10;
      }
      
      SetPanel("SpreadColumnBox", 0, x_axis, y_axis, 33, 19, Box_Color, BoarderColor, 1);
      SetText("SpreadColumn", "Sprd", x_axis+3, y_axis+2, clrWhite, 8);
      SetPanel("SpreadBox"+IntegerToString(i), 0, x_axis, (i*22)+y_axis+22, 33, 19, Box_Color, BoarderColor, 1);
      SetText("Spread"+IntegerToString(i), "0.0", x_axis+5, (i*22)+y_axis+24, clrOrange, 8);
      
      SetPanel("PipsColumnBox", 0, x_axis+35, y_axis, 33, 19, Box_Color, BoarderColor, 1);
      SetText("PipsColumn", "Pips", x_axis+39, y_axis+2, clrWhite, 8);
      SetPanel("PipsBox"+IntegerToString(i), 0, x_axis+35, (i*22)+y_axis+22, 33, 19, Box_Color, BoarderColor, 1);
      SetText("Pips"+IntegerToString(i), "0", x_axis+39, (i*22)+y_axis+24, clrOrange, 8);
      
      SetPanel("PercentageChangeBox", 0, x_axis+70, y_axis, 33, 19, Box_Color, BoarderColor, 1);
      SetText("PercentageChangeColumn", "%Ch", x_axis+73, y_axis+2, clrWhite, 8);
      SetPanel("PercentageChangeBox"+IntegerToString(i), 0, x_axis+70, (i*22)+y_axis+22, 33, 19, Box_Color, BoarderColor, 1);
      SetText("PercentageChange"+IntegerToString(i), "0%", x_axis+73, (i*22)+y_axis+24, clrOrange, 8);
      
      SetPanel("ATRColumnBox", 0, x_axis+105, y_axis, 33, 19, Box_Color, BoarderColor, 1);
      SetText("ATRColumn", "ATR", x_axis+109, y_axis+2, clrWhite, 8);
      SetPanel("ATRBox"+IntegerToString(i), 0, x_axis+105, (i*22)+y_axis+22, 33, 19, Box_Color, BoarderColor, 1);
      SetText("ATR"+IntegerToString(i), "0", x_axis+109, (i*22)+y_axis+24, clrOrange, 8);
      
      SetPanel("ProbColumnBox", 0, x_axis+140, y_axis, 33, 19, Box_Color, BoarderColor, 1);
      SetText("ProbColumn", "Prob", x_axis+144, y_axis+2, clrWhite, 8);
      SetPanel("ProbBox"+IntegerToString(i), 0, x_axis+140, (i*22)+y_axis+22, 33, 19, Box_Color, BoarderColor, 1);
      SetText("Prob"+IntegerToString(i), "0.00", x_axis+144, (i*22)+y_axis+24, clrOrange, 8);
      
      SetPanel("RangeColumnBox", 0, x_axis+193, y_axis, 53, 19, Box_Color, BoarderColor, 1);
      SetText("RangeColumn", "Range", x_axis+197, y_axis+2, clrWhite, 8);
      SetPanel("RangeBox"+IntegerToString(i), 0, x_axis+193, (i*22)+y_axis+22, 53, 19, Box_Color, BoarderColor, 1);
      SetText("Range"+IntegerToString(i), "0.00", x_axis+197, (i*22)+y_axis+24, clrOrange, 8);
      
      SetPanel("HighColumnBox", 0, x_axis+248, y_axis, 53, 19, Box_Color, BoarderColor, 1);
      SetText("HighColumn", "High", x_axis+252, y_axis+2, clrWhite, 8);
      SetPanel("HighBox"+IntegerToString(i), 0, x_axis+248, (i*22)+y_axis+22, 53, 19, Box_Color, BoarderColor, 1);
      SetText("High"+IntegerToString(i), "0.00", x_axis+252, (i*22)+y_axis+24, clrOrange, 8);
      
      SetPanel("LowColumnBox", 0, x_axis+303, y_axis, 53, 19, Box_Color, BoarderColor, 1);
      SetText("LowColumn", "Low", x_axis+307, y_axis+2, clrWhite, 8);
      SetPanel("LowBox"+IntegerToString(i), 0, x_axis+303, (i*22)+y_axis+22, 53, 19, Box_Color, BoarderColor, 1);
      SetText("Low"+IntegerToString(i), "0.00", x_axis+307, (i*22)+y_axis+24, clrOrange, 8);
      
      if(i == 0)
      {
         SetPanel("ATRValLabel", 0, x_axis, y_axis+43, 40, 19, Box_Color, BoarderColor, 1);
         SetText("ATRValLabelText", "ATR", x_axis+10, y_axis+45, clrWhite, 8);
         
         for(int k = 0; k < 8; k++)
         {
            int xPos = x_axis + 42 + (k * 39);
            SetPanel("ATRValH"+IntegerToString(k), 0, xPos, y_axis+43, 39, 19, Box_Color, BoarderColor, 1);
         }
         
         SetText("ATRValH0Text", "M1", x_axis+51, y_axis+45, clrWhite, 8);
         SetText("ATRValH1Text", "M5", x_axis+90, y_axis+45, clrWhite, 8);
         SetText("ATRValH2Text", "M15", x_axis+127, y_axis+45, clrWhite, 8);
         SetText("ATRValH3Text", "H1", x_axis+168, y_axis+45, clrWhite, 8);
         SetText("ATRValH4Text", "H4", x_axis+207, y_axis+45, clrWhite, 8);
         SetText("ATRValH5Text", "D1", x_axis+246, y_axis+45, clrWhite, 8);
         SetText("ATRValH6Text", "W", x_axis+287, y_axis+45, clrWhite, 8);
         SetText("ATRValH7Text", "M", x_axis+327, y_axis+45, clrWhite, 8);
      }
      
      SetPanel("PercentATRBox"+IntegerToString(i), 0, x_axis, (i*22)+y_axis+64, 40, 19, Box_Color, BoarderColor, 1);
      SetText("PercentATRLabel"+IntegerToString(i), "0%", x_axis+8, (i*22)+y_axis+66, clrYellow, 8);
      
      for(int k = 0; k < 8; k++)
      {
         int xPos = x_axis + 42 + (k * 39);
         SetPanel("ATRVal"+IntegerToString(k)+"_"+IntegerToString(i), 0, xPos, (i*22)+y_axis+64, 39, 19, Box_Color, BoarderColor, 1);
         SetText("ATRValText"+IntegerToString(k)+"_"+IntegerToString(i), "0", xPos+3, (i*22)+y_axis+66, clrYellow, 8);
      }
      
      SetPanel("ProductBox"+IntegerToString(i), 0, x_axis, (i*22)+y_axis+86, 80, 19, clrOrange, BoarderColor, 1);
      SetText("Product"+IntegerToString(i), TradePairs[i], x_axis+6, (i*22)+y_axis+88, clrBlack, 8);
      
      for(int box = 1; box <= 25; box++)
      {
         int boxY = ((i*22)+y_axis+86) + ((box-1) * 21);
         
         SetPanel("PriceTF_"+IntegerToString(box)+"_"+IntegerToString(i), 0, x_axis+303, boxY, 20, 19, Box_Color, BoarderColor, 1);
         SetText("PriceTF_"+IntegerToString(box)+"_text_"+IntegerToString(i), "--", x_axis+305, boxY+3, clrGray, 7);
         
         SetPanel("PriceType_"+IntegerToString(box)+"_"+IntegerToString(i), 0, x_axis+323, boxY, 15, 19, Box_Color, BoarderColor, 1);
         SetText("PriceType_"+IntegerToString(box)+"_text_"+IntegerToString(i), "-", x_axis+326, boxY+3, clrGray, 7);
         
         SetPanel("PriceBox_"+IntegerToString(box)+"_"+IntegerToString(i), 0, x_axis+338, boxY, 50, 19, Box_Color, BoarderColor, 1);
         SetText("PriceBox_"+IntegerToString(box)+"_text_"+IntegerToString(i), "0.00", x_axis+340, boxY+3, clrOrange, 8);
      }
   }
   
   BuildSignalBoard();
   EventSetTimer(1);
   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Build Main Signal Matrix Dashboard                               |
//| Creates 27-row indicator panel with 8 timeframe columns          |
//| Additional 3 columns for Short/Medium/Long term summaries        |
//| Row labels identify each indicator type                          |
//+------------------------------------------------------------------+
void BuildSignalBoard()
{
   int baseY = y_axis + 107;
   int rowHeight = 21;
   
   SetPanel("TF_Box_M1", 0, x_axis+83, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("TF_Box_M5", 0, x_axis+102, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("TF_Box_M15", 0, x_axis+121, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("TF_Box_H1", 0, x_axis+140, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("TF_Box_H4", 0, x_axis+159, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("TF_Box_D1", 0, x_axis+178, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("TF_Box_W1", 0, x_axis+197, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("TF_Box_MN", 0, x_axis+216, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("GMMA_Box_S", 0, x_axis+239, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("GMMA_Box_M", 0, x_axis+258, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   SetPanel("GMMA_Box_L", 0, x_axis+277, baseY-21, 19, 19, Box_Color, BoarderColor, 1);
   
   string rowLabels[] = {" %ATR", " T-K Distance", " SAR", " RSI", " MACD", " ADX", " Pr v Cloud", " Pr v Kijun", " Pr v Tenkan", " Pr v Span A", " Pr v Span B", " Tenkan Slope", " Kijun Slope", " T v K", " Tenkan v Cloud", " Kijun v Cloud", " Cloud Color", " Thick Trend", " Chikou v Price", " Chikou v Cloud", " Cloud Twist", " Cloud Thick", " Chikou Space", " T/Cloud Arrow", " K/Cloud Arrow", " Cloud Switch", " MACD Arrow"};
   
   color rowColors[] = {clrYellow, clrYellow, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrWhite, clrYellow, clrYellow, clrWhite, clrWhite, clrWhite, clrWhite};
   
   for(int row = 0; row < 27; row++)
   {
      SetPanel("Row"+IntegerToString(row)+"_Label", 0, x_axis, baseY + (row*rowHeight), 80, 19, Box_Color, BoarderColor, 1);
      SetText("Row"+IntegerToString(row)+"_Text", rowLabels[row], x_axis+3, baseY + (row*rowHeight)+2, rowColors[row], 8);
      
      for(int col = 0; col < 8; col++)
      {
         int xPos = x_axis + 82 + (col * 19);
         int yPos = baseY + (row * rowHeight);
         SetPanel("Indicator_R"+IntegerToString(row)+"_TF"+IntegerToString(col), 0, xPos, yPos, 19, 19, Box_Color, BoarderColor, 1);
      }
      
      for(int col = 0; col < 3; col++)
      {
         int xPos = x_axis + 239 + (col * 19);
         int yPos = baseY + (row * rowHeight);
         SetPanel("Summary_R"+IntegerToString(row)+"_T"+IntegerToString(col), 0, xPos, yPos, 19, 19, Box_Color, BoarderColor, 1);
      }
   }
   
   SetText("TF_M1", "1", x_axis+89, baseY-18, clrWhite, 8);
   SetText("TF_M5", "5", x_axis+108, baseY-18, clrWhite, 8);
   SetText("TF_M15", "15", x_axis+124, baseY-18, clrWhite, 8);
   SetText("TF_H1", "H1", x_axis+143, baseY-18, clrWhite, 8);
   SetText("TF_H4", "H4", x_axis+162, baseY-18, clrWhite, 8);
   SetText("TF_D1", "D1", x_axis+182, baseY-18, clrWhite, 8);
   SetText("TF_W1", "W", x_axis+202, baseY-18, clrWhite, 8);
   SetText("TF_MN", "M", x_axis+221, baseY-18, clrWhite, 8);
   SetText("Sum_S", "S", x_axis+244, baseY-18, clrWhite, 8);
   SetText("Sum_M", "M", x_axis+263, baseY-18, clrWhite, 8);
   SetText("Sum_L", "L", x_axis+283, baseY-18, clrWhite, 8);
}

//+------------------------------------------------------------------+
//| Expert Advisor Deinitialization Function                         |
//| Cleanup operations when EA is removed from chart                 |
//| Stops timer and removes all graphical objects                    |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   EventKillTimer();
   ObjectsDeleteAll();
}

//+------------------------------------------------------------------+
//| Timer Event Handler - Main Processing Loop                       |
//| Executed every second (or per DashUpdate setting)                |
//| Processing sequence:                                             |
//| 1. Calculate all technical indicator signals                     |
//| 2. Aggregate multi-timeframe trend consensus                     |
//| 3. Compute quantitative probability scores                       |
//| 4. Update ATR values across timeframes                           |
//| 5. Apply color coding to all indicators                          |
//| 6. Render price action ladder and signals                        |
//| 7. Display arrow indicators for breakout conditions              |
//| 8. Check and dispatch filtered alert signals                     |
//| 9. Force chart redraw to update display                          |
//+------------------------------------------------------------------+
void OnTimer()
{
   GetAllSignals();
   GetTrendStatus();
   GetTradingProb();
   DisplayATRValues();
   SetAllColors();
   SetIndicatorColor();
   PlotPriceAction();
   Display_TC_Arrows();
   Display_KC_Arrows();
   Display_Twist_Arrows();
   Display_MACD_Arrows();
   
   Check_And_Send_Arrow_Alerts();
   
   WindowRedraw();
}

//+------------------------------------------------------------------+
//| Calculate All Indicator Signals                                  |
//| Iterates through all trading pairs and 8 timeframes              |
//| Calls individual calculation functions for each indicator        |
//| Stores results in parallel arrays for downstream processing      |
//+------------------------------------------------------------------+
void GetAllSignals()
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         int timeframe = NumberToTimeframe(j);
         
         ATR[i][j] = Get_ATR_Signals(TradePairs[i], timeframe, 0);
         SAR[i][j] = Get_SAR_Signals(TradePairs[i], timeframe, 1);
         MACD[i][j] = Get_MACD_Signals(TradePairs[i], timeframe, 0);
         RSI[i][j] = Get_RSI_Signals(TradePairs[i], timeframe, 0);
         ADX[i][j] = Get_ADX_Signals(TradePairs[i], timeframe, 0);
         PT[i][j] = Get_PT_Signals(TradePairs[i], timeframe, 0);
         TSlope[i][j] = Get_TSlope_Signals(TradePairs[i], timeframe, 0);
         TK[i][j] = Get_TK_Signals(TradePairs[i], timeframe, 0);
         TKDelta[i][j] = Get_TKDelta_Signals(TradePairs[i], timeframe, 0);
         PK[i][j] = Get_PK_Signals(TradePairs[i], timeframe, 0);
         KSlope[i][j] = Get_KSlope_Signals(TradePairs[i], timeframe, 0);
         ChP[i][j] = Get_ChP_Signals(TradePairs[i], timeframe, 0);
         PA[i][j] = Get_PA_Signals(TradePairs[i], timeframe, 0);
         PC[i][j] = Get_PC_Signals(TradePairs[i], timeframe, 0);
         PB[i][j] = Get_PB_Signals(TradePairs[i], timeframe, 0);
         TC[i][j] = Get_TC_Signals(TradePairs[i], timeframe, 0);
         KC[i][j] = Get_KC_Signals(TradePairs[i], timeframe, 0);
         Kumo[i][j] = Get_Kumo_Signals(TradePairs[i], timeframe, 0);
         Thick[i][j] = Get_Thick_Signals(TradePairs[i], timeframe, 0);
         DeltaThick[i][j] = Get_DeltaThick_Signals(TradePairs[i], timeframe, 0);
         ChC[i][j] = Get_ChC_Signals(TradePairs[i], timeframe, 0);
         ChFree[i][j] = Get_ChFree_Signals(TradePairs[i], timeframe, 0);
         Twist[i][j] = Get_Twist_Signals(TradePairs[i], timeframe, 0);
         
         TC_Arrow[i][j] = Get_TC_Arrow_Signals(TradePairs[i], timeframe, 1);
         KC_Arrow[i][j] = Get_KC_Arrow_Signals(TradePairs[i], timeframe, 1);
         Twist_Arrow[i][j] = Get_Twist_Arrow_Signals(TradePairs[i], timeframe, 1);
         MACD_Arrow[i][j] = Get_MACD_Arrow_Signals(TradePairs[i], timeframe, 1);
      }
   }
}

//+------------------------------------------------------------------+
//| Aggregate Multi-Timeframe Trend Status                           |
//| Processes each indicator to determine consensus across TF groups |
//| Creates summary trends: Short (M1-M15), Medium (M15-H4), Long    |
//| Special handling for ATR: only UP state is meaningful            |
//+------------------------------------------------------------------+
void GetTrendStatus()
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      TrendStatus(ATR, i); TrendStatus(SAR, i); TrendStatus(MACD, i); TrendStatus(RSI, i); TrendStatus(ADX, i);
      TrendStatus(PT, i); TrendStatus(TSlope, i); TrendStatus(TK, i); TrendStatus(TKDelta, i); TrendStatus(PK, i);
      TrendStatus(KSlope, i); TrendStatus(ChP, i); TrendStatus(PA, i); TrendStatus(PC, i); TrendStatus(PB, i);
      TrendStatus(TC, i); TrendStatus(KC, i); TrendStatus(Kumo, i); TrendStatus(Thick, i); TrendStatus(DeltaThick, i);
      TrendStatus(ChC, i); TrendStatus(ChFree, i); TrendStatus(Twist, i);
      
      if(ATR[i][8] != UP) ATR[i][8] = NONE;
      if(ATR[i][9] != UP) ATR[i][9] = NONE;
      if(ATR[i][10] != UP) ATR[i][10] = NONE;
   }
}

//+------------------------------------------------------------------+
//| Calculate Trend Consensus for Single Indicator                   |
//| Indices 8-10 store aggregated trends:                            |
//| Index 8: Short-term (M1, M5, M15)                                |
//| Index 9: Medium-term (M15, H1, H4)                               |
//| Index 10: Long-term (D1, W1, MN)                                 |
//| Requires unanimous agreement for consensus signal                |
//+------------------------------------------------------------------+
void TrendStatus(int &array[][], int i)
{
   array[i][8] = NONE; array[i][9] = NONE; array[i][10] = NONE;
   
   if(array[i][0] == UP && array[i][1] == UP && array[i][2] == UP) array[i][8] = UP;
   else if(array[i][0] == DOWN && array[i][1] == DOWN && array[i][2] == DOWN) array[i][8] = DOWN;
   
   if(array[i][2] == UP && array[i][3] == UP && array[i][4] == UP) array[i][9] = UP;
   else if(array[i][2] == DOWN && array[i][3] == DOWN && array[i][4] == DOWN) array[i][9] = DOWN;
   
   if(array[i][5] == UP && array[i][6] == UP && array[i][7] == UP) array[i][10] = UP;
   else if(array[i][5] == DOWN && array[i][6] == DOWN && array[i][7] == DOWN) array[i][10] = DOWN;
}

//+------------------------------------------------------------------+
//| Calculate and Display ATR Values in Pips                         |
//| Converts raw ATR to pip-normalized values for each timeframe     |
//| Handles different decimal precision (2/4 digit vs 3/5 digit)     |
//| Dynamic formatting: 1 decimal if <10, integer if >100            |
//+------------------------------------------------------------------+
void DisplayATRValues()
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         int timeframe = NumberToTimeframe(j);
         double atr_raw = iATR(TradePairs[i], timeframe, ATR_Period, 0);
         
         if(pairinfo[i].PairPip > 0 && pairinfo[i].pipsfactor > 0)
            ATR_Values[i][j] = atr_raw / pairinfo[i].PairPip / pairinfo[i].pipsfactor;
         else
            ATR_Values[i][j] = 0;
         
         string atr_text = (ATR_Values[i][j] < 10) ? DoubleToStr(ATR_Values[i][j], 1) : 
                          ((ATR_Values[i][j] < 100) ? DoubleToStr(ATR_Values[i][j], 0) : IntegerToString((int)ATR_Values[i][j]));
         
         ObjectSetText("ATRValText"+IntegerToString(j)+"_"+IntegerToString(i), atr_text, 8, NULL, clrYellow);
      }
   }
}

//+------------------------------------------------------------------+
//| Plot Price Action Dashboard with Multi-Column Price Ladder       |
//| Processing steps:                                                |
//| 1. Collect 24 Ichimoku price levels (8 TF x 3 types)            |
//| 2. Add current market price as 25th element                      |
//| 3. Sort all levels in descending order                           |
//| 4. Calculate daily statistics (pips, %, ATR, range)              |
//| 5. Render sorted price ladder with TF/Type/Price columns         |
//| 6. Apply color coding based on price position (S/R zones)        |
//| 7. Highlight current price and nearby levels                     |
//+------------------------------------------------------------------+
void PlotPriceAction()
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      int idx = 0;
      
      for(int tf = 0; tf < 8; tf++)
      {
         int period = NumberToTimeframe(tf);
         price_levels[idx].price = iIchimoku(TradePairs[i], period, tenkan, kijun, senkou, MODE_KIJUNSEN, 0);
         price_levels[idx].timeframe = tf;
         price_levels[idx].type = 0;
         idx++;
      }
      
      for(int tf = 0; tf < 8; tf++)
      {
         int period = NumberToTimeframe(tf);
         price_levels[idx].price = iIchimoku(TradePairs[i], period, tenkan, kijun, senkou, MODE_SENKOUSPANA, 0);
         price_levels[idx].timeframe = tf;
         price_levels[idx].type = 1;
         idx++;
      }
      
      for(int tf = 0; tf < 8; tf++)
      {
         int period = NumberToTimeframe(tf);
         price_levels[idx].price = iIchimoku(TradePairs[i], period, tenkan, kijun, senkou, MODE_SENKOUSPANB, 0);
         price_levels[idx].timeframe = tf;
         price_levels[idx].type = 2;
         idx++;
      }
      
      price_levels[24].price = iClose(TradePairs[i], PERIOD_CURRENT, 0);
      price_levels[24].timeframe = -1;
      price_levels[24].type = 3;
      
      SortPriceLevels();
      
      for(int k = 0; k < 25; k++)
      {
         reference_price[k] = price_levels[k].price;
      }
      
      if(MarketInfo(TradePairs[i], MODE_POINT) != 0 && pairinfo[i].pipsfactor != 0)
      {
         pairinfo[i].Spread = MarketInfo(TradePairs[i], MODE_SPREAD) / pairinfo[i].pipsfactor;
         pairinfo[i].Pips = (iClose(TradePairs[i], PERIOD_D1, 0) - iOpen(TradePairs[i], PERIOD_D1, 0)) / MarketInfo(TradePairs[i], MODE_POINT) / pairinfo[i].pipsfactor;
         pairinfo[i].PercentageChange = (iClose(TradePairs[i], PERIOD_D1, 0) - iOpen(TradePairs[i], PERIOD_D1, 0)) / iOpen(TradePairs[i], PERIOD_D1, 0) * 100;
         pairinfo[i].atr = iATR(TradePairs[i], PERIOD_D1, ATR_Period, 0) / pairinfo[i].pipsfactor;
         pairinfo[i].dayHigh = iHigh(TradePairs[i], PERIOD_D1, 0);
         pairinfo[i].dayLow = iLow(TradePairs[i], PERIOD_D1, 0);
      }
      
      PipsColor = (pairinfo[i].Pips > 0) ? LightBullColor : ((pairinfo[i].Pips < 0) ? LightBearColor : clrWhite);
      Prob_Color = (pairinfo[i].Trading_Prob > 0) ? LightBullColor : ((pairinfo[i].Trading_Prob < 0) ? LightBearColor : clrWhite);
      
      int digits = (int)MarketInfo(TradePairs[i], MODE_DIGITS);
      string atr_display = (digits < 4) ? DoubleToStr(pairinfo[i].atr * 10, 0) : DoubleToStr(pairinfo[i].atr * 10000, 0);
      
      ObjectSetText("Spread"+IntegerToString(i), DoubleToStr(pairinfo[i].Spread, 1), 8, NULL, clrOrange);
      ObjectSetText("Pips"+IntegerToString(i), DoubleToStr(MathAbs(pairinfo[i].Pips), 0), 8, NULL, PipsColor);
      ObjectSetText("PercentageChange"+IntegerToString(i), DoubleToStr(MathAbs(pairinfo[i].PercentageChange), 2), 8, NULL, PipsColor);
      ObjectSetText("ATR"+IntegerToString(i), atr_display, 8, NULL, clrYellow);
      ObjectSetText("Prob"+IntegerToString(i), DoubleToStr(MathAbs(pairinfo[i].Trading_Prob), 2), 8, NULL, Prob_Color);
      ObjectSetText("High"+IntegerToString(i), DoubleToStr(pairinfo[i].dayHigh, (digits >= 4) ? 2 : digits), 8, NULL, LightBullColor);
      ObjectSetText("Low"+IntegerToString(i), DoubleToStr(pairinfo[i].dayLow, (digits >= 4) ? 2 : digits), 8, NULL, LightBearColor);
      ObjectSetText("Range"+IntegerToString(i), DoubleToStr(pairinfo[i].dayHigh - pairinfo[i].dayLow, (digits >= 4) ? 2 : digits), 8, NULL, clrYellow);
      
      double daily_atr_pct = Calculate_Daily_ATR_Percent(i);
      string atr_pct_display = IntegerToString((int)MathRound(daily_atr_pct)) + "%";
      ObjectSetText("PercentATRLabel"+IntegerToString(i), atr_pct_display, 8, NULL, clrYellow);
      
      double current_price_live = iClose(TradePairs[i], PERIOD_CURRENT, 0);
      
      int price_precision = (digits >= 4) ? 2 : digits;
      
      for(int k = 0; k < 25; k++)
      {
         color price_color;
         color tf_color;
         color type_color;
         
         bool is_current_price = (price_levels[k].type == 3);
         bool is_near_price = (MathAbs(price_levels[k].price - current_price_live) < pairinfo[i].PairPip * 10);
         bool is_above_price = (price_levels[k].price > current_price_live);
         
         if(is_current_price)
         {
            price_color = clrYellow;
            tf_color = clrYellow;
            type_color = clrYellow;
         }
         else if(is_near_price)
         {
            price_color = clrYellow;
            tf_color = clrWhite;
            type_color = clrWhite;
         }
         else if(is_above_price)
         {
            price_color = LightBearColor;
            tf_color = clrGray;
            type_color = clrGray;
         }
         else
         {
            price_color = LightBullColor;
            tf_color = clrGray;
            type_color = clrGray;
         }
         
         if(!is_current_price && !is_near_price)
         {
            int tf_idx = price_levels[k].timeframe;
            if(tf_idx >= 5)
            {
               tf_color = clrWhite;
               type_color = clrWhite;
            }
            else if(tf_idx >= 3)
            {
               tf_color = clrSilver;
               type_color = clrSilver;
            }
         }
         
         string tf_text;
         if(is_current_price)
            tf_text = ">>";
         else
            tf_text = GetTFShortName(price_levels[k].timeframe);
         
         ObjectSetText("PriceTF_"+IntegerToString(k+1)+"_text_"+IntegerToString(i), tf_text, 7, NULL, tf_color);
         
         string type_text = GetTypeShortName(price_levels[k].type);
         ObjectSetText("PriceType_"+IntegerToString(k+1)+"_text_"+IntegerToString(i), type_text, 7, NULL, type_color);
         
         ObjectSetText("PriceBox_"+IntegerToString(k+1)+"_text_"+IntegerToString(i), 
                       DoubleToStr(price_levels[k].price, price_precision), 8, NULL, price_color);
      }
   }
}

//+------------------------------------------------------------------+
//| Apply Color Coding to All Indicator Cells                        |
//| Maps signal states to visual colors for dashboard display        |
//| Individual timeframes: dark bull/bear colors                     |
//| Consensus summaries: light bull/bear colors                      |
//| Special indicators (ATR, TKDelta, etc.): yellow when active      |
//+------------------------------------------------------------------+
void SetAllColors()
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         if(ATR[i][j] == UP) ATR_color[i][j] = clrYellow; else ATR_color[i][j] = clrNONE;
         if(TKDelta[i][j] == UP) TKDelta_color[i][j] = clrYellow; else TKDelta_color[i][j] = clrNONE;
         if(Thick[i][j] == UP) Thick_color[i][j] = clrYellow; else Thick_color[i][j] = clrNONE;
         if(ChFree[i][j] == UP) ChFree_color[i][j] = clrYellow; else ChFree_color[i][j] = clrNONE;
         
         TrendColorStatus(PT, PT_color, i, j, BullColor, BearColor);
         TrendColorStatus(TSlope, TSlope_color, i, j, BullColor, BearColor);
         TrendColorStatus(SAR, SAR_color, i, j, BullColor, BearColor);
         TrendColorStatus(RSI, RSI_color, i, j, BullColor, BearColor);
         TrendColorStatus(MACD, MACD_color, i, j, BullColor, BearColor);
         TrendColorStatus(ADX, ADX_color, i, j, BullColor, BearColor);
         TrendColorStatus(TK, TK_color, i, j, BullColor, BearColor);
         TrendColorStatus(PK, PK_color, i, j, BullColor, BearColor);
         TrendColorStatus(KSlope, KSlope_color, i, j, BullColor, BearColor);
         TrendColorStatus(ChP, ChP_color, i, j, BullColor, BearColor);
         TrendColorStatus(PA, PA_color, i, j, BullColor, BearColor);
         TrendColorStatus(PC, PC_color, i, j, BullColor, BearColor);
         TrendColorStatus(PB, PB_color, i, j, BullColor, BearColor);
         TrendColorStatus(TC, TC_color, i, j, BullColor, BearColor);
         TrendColorStatus(KC, KC_color, i, j, BullColor, BearColor);
         TrendColorStatus(Kumo, Kumo_color, i, j, BullColor, BearColor);
         TrendColorStatus(DeltaThick, DeltaThick_color, i, j, BullColor, BearColor);
         TrendColorStatus(ChC, ChC_color, i, j, BullColor, BearColor);
         TrendColorStatus(Twist, Twist_color, i, j, BullColor, BearColor);
      }
      
      for(int j = 8; j < 11; j++)
      {
         if(ATR[i][j] == UP) ATR_color[i][j] = clrGold; else ATR_color[i][j] = clrNONE;
         if(TKDelta[i][j] == UP) TKDelta_color[i][j] = clrGold; else TKDelta_color[i][j] = clrNONE;
         if(Thick[i][j] == UP) Thick_color[i][j] = clrGold; else Thick_color[i][j] = clrNONE;
         if(ChFree[i][j] == UP) ChFree_color[i][j] = clrGold; else ChFree_color[i][j] = clrNONE;
         
         TrendColorStatus(PT, PT_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(TSlope, TSlope_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(SAR, SAR_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(RSI, RSI_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(MACD, MACD_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(ADX, ADX_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(TK, TK_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(PK, PK_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(KSlope, KSlope_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(ChP, ChP_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(PA, PA_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(PC, PC_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(PB, PB_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(TC, TC_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(KC, KC_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(Kumo, Kumo_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(DeltaThick, DeltaThick_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(ChC, ChC_color, i, j, LightBullColor, LightBearColor);
         TrendColorStatus(Twist, Twist_color, i, j, LightBullColor, LightBearColor);
      }
      
      for(int j = 0; j < 8; j++)
      {
         ColorPanel("Indicator_R0_TF"+IntegerToString(j), ATR_color[i][j], BoarderColor);
         ColorPanel("Indicator_R1_TF"+IntegerToString(j), TKDelta_color[i][j], BoarderColor);
         ColorPanel("Indicator_R2_TF"+IntegerToString(j), SAR_color[i][j], BoarderColor);
         ColorPanel("Indicator_R3_TF"+IntegerToString(j), RSI_color[i][j], BoarderColor);
         ColorPanel("Indicator_R4_TF"+IntegerToString(j), MACD_color[i][j], BoarderColor);
         ColorPanel("Indicator_R5_TF"+IntegerToString(j), ADX_color[i][j], BoarderColor);
         ColorPanel("Indicator_R6_TF"+IntegerToString(j), PC_color[i][j], BoarderColor);
         ColorPanel("Indicator_R7_TF"+IntegerToString(j), PK_color[i][j], BoarderColor);
         ColorPanel("Indicator_R8_TF"+IntegerToString(j), PT_color[i][j], BoarderColor);
         ColorPanel("Indicator_R9_TF"+IntegerToString(j), PA_color[i][j], BoarderColor);
         ColorPanel("Indicator_R10_TF"+IntegerToString(j), PB_color[i][j], BoarderColor);
         ColorPanel("Indicator_R11_TF"+IntegerToString(j), TSlope_color[i][j], BoarderColor);
         ColorPanel("Indicator_R12_TF"+IntegerToString(j), KSlope_color[i][j], BoarderColor);
         ColorPanel("Indicator_R13_TF"+IntegerToString(j), TK_color[i][j], BoarderColor);
         ColorPanel("Indicator_R14_TF"+IntegerToString(j), TC_color[i][j], BoarderColor);
         ColorPanel("Indicator_R15_TF"+IntegerToString(j), KC_color[i][j], BoarderColor);
         ColorPanel("Indicator_R16_TF"+IntegerToString(j), Kumo_color[i][j], BoarderColor);
         ColorPanel("Indicator_R17_TF"+IntegerToString(j), DeltaThick_color[i][j], BoarderColor);
         ColorPanel("Indicator_R18_TF"+IntegerToString(j), ChP_color[i][j], BoarderColor);
         ColorPanel("Indicator_R19_TF"+IntegerToString(j), ChC_color[i][j], BoarderColor);
         ColorPanel("Indicator_R20_TF"+IntegerToString(j), Twist_color[i][j], BoarderColor);
         ColorPanel("Indicator_R21_TF"+IntegerToString(j), Thick_color[i][j], BoarderColor);
         ColorPanel("Indicator_R22_TF"+IntegerToString(j), ChFree_color[i][j], BoarderColor);
         ColorPanel("Indicator_R23_TF"+IntegerToString(j), clrNONE, BoarderColor);
         ColorPanel("Indicator_R24_TF"+IntegerToString(j), clrNONE, BoarderColor);
         ColorPanel("Indicator_R25_TF"+IntegerToString(j), clrNONE, BoarderColor);
         ColorPanel("Indicator_R26_TF"+IntegerToString(j), clrNONE, BoarderColor);
      }
      
      for(int j = 0; j < 3; j++)
      {
         ColorPanel("Summary_R0_T"+IntegerToString(j), ATR_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R1_T"+IntegerToString(j), TKDelta_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R2_T"+IntegerToString(j), SAR_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R3_T"+IntegerToString(j), RSI_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R4_T"+IntegerToString(j), MACD_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R5_T"+IntegerToString(j), ADX_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R6_T"+IntegerToString(j), PC_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R7_T"+IntegerToString(j), PK_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R8_T"+IntegerToString(j), PT_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R9_T"+IntegerToString(j), PA_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R10_T"+IntegerToString(j), PB_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R11_T"+IntegerToString(j), TSlope_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R12_T"+IntegerToString(j), KSlope_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R13_T"+IntegerToString(j), TK_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R14_T"+IntegerToString(j), TC_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R15_T"+IntegerToString(j), KC_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R16_T"+IntegerToString(j), Kumo_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R17_T"+IntegerToString(j), DeltaThick_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R18_T"+IntegerToString(j), ChP_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R19_T"+IntegerToString(j), ChC_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R20_T"+IntegerToString(j), Twist_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R21_T"+IntegerToString(j), Thick_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R22_T"+IntegerToString(j), ChFree_color[i][j+8], BoarderColor);
         ColorPanel("Summary_R23_T"+IntegerToString(j), clrNONE, BoarderColor);
         ColorPanel("Summary_R24_T"+IntegerToString(j), clrNONE, BoarderColor);
         ColorPanel("Summary_R25_T"+IntegerToString(j), clrNONE, BoarderColor);
         ColorPanel("Summary_R26_T"+IntegerToString(j), clrNONE, BoarderColor);
      }
   }
}

//+------------------------------------------------------------------+
//| Map Signal State to Display Color                                |
//| Assigns bullish or bearish color based on indicator direction    |
//+------------------------------------------------------------------+
void TrendColorStatus(int &array[][], int &array_color[][], int i, int j, color bullcolor, color bearcolor)
{
   if(array[i][j] == UP) array_color[i][j] = bullcolor;
   else if(array[i][j] == DOWN) array_color[i][j] = bearcolor;
   else array_color[i][j] = clrNONE;
}

//+------------------------------------------------------------------+
//| Update Indicator Row Label Colors Based on Consensus             |
//| Highlights row labels when 2 of 3 timeframe groups agree         |
//| Also calculates timeframe box colors (requires all 5 indicators) |
//| Updates Short/Medium/Long term summary boxes                     |
//+------------------------------------------------------------------+
void SetIndicatorColor()
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      int baseY = y_axis + 107;
      
      int ATR_consensus = NONE;
      if((ATR[i][8]==UP && ATR[i][9]==UP) || (ATR[i][8]==UP && ATR[i][10]==UP) || (ATR[i][9]==UP && ATR[i][10]==UP))
         ATR_consensus = UP;
      SetPanel("Row0_Label", 0, x_axis, baseY+(0*21), 80, 19, (ATR_consensus==UP) ? clrYellow : Box_Color, BoarderColor, 1);
      
      int TKDelta_consensus = NONE;
      if((TKDelta[i][8]==UP && TKDelta[i][9]==UP) || (TKDelta[i][8]==UP && TKDelta[i][10]==UP) || (TKDelta[i][9]==UP && TKDelta[i][10]==UP))
         TKDelta_consensus = UP;
      SetPanel("Row1_Label", 0, x_axis, baseY+(1*21), 80, 19, (TKDelta_consensus==UP) ? clrYellow : Box_Color, BoarderColor, 1);
      
      int SAR_consensus = NONE;
      if((SAR[i][8]==UP && SAR[i][9]==UP) || (SAR[i][8]==UP && SAR[i][10]==UP) || (SAR[i][9]==UP && SAR[i][10]==UP))
         SAR_consensus = UP;
      else if((SAR[i][8]==DOWN && SAR[i][9]==DOWN) || (SAR[i][8]==DOWN && SAR[i][10]==DOWN) || (SAR[i][9]==DOWN && SAR[i][10]==DOWN))
         SAR_consensus = DOWN;
      SetPanel("Row2_Label", 0, x_axis, baseY+(2*21), 80, 19, (SAR_consensus==UP) ? BullColor : ((SAR_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int RSI_consensus = NONE;
      if((RSI[i][8]==UP && RSI[i][9]==UP) || (RSI[i][8]==UP && RSI[i][10]==UP) || (RSI[i][9]==UP && RSI[i][10]==UP))
         RSI_consensus = UP;
      else if((RSI[i][8]==DOWN && RSI[i][9]==DOWN) || (RSI[i][8]==DOWN && RSI[i][10]==DOWN) || (RSI[i][9]==DOWN && RSI[i][10]==DOWN))
         RSI_consensus = DOWN;
      SetPanel("Row3_Label", 0, x_axis, baseY+(3*21), 80, 19, (RSI_consensus==UP) ? BullColor : ((RSI_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int MACD_consensus = NONE;
      if((MACD[i][8]==UP && MACD[i][9]==UP) || (MACD[i][8]==UP && MACD[i][10]==UP) || (MACD[i][9]==UP && MACD[i][10]==UP))
         MACD_consensus = UP;
      else if((MACD[i][8]==DOWN && MACD[i][9]==DOWN) || (MACD[i][8]==DOWN && MACD[i][10]==DOWN) || (MACD[i][9]==DOWN && MACD[i][10]==DOWN))
         MACD_consensus = DOWN;
      SetPanel("Row4_Label", 0, x_axis, baseY+(4*21), 80, 19, (MACD_consensus==UP) ? BullColor : ((MACD_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int ADX_consensus = NONE;
      if((ADX[i][8]==UP && ADX[i][9]==UP) || (ADX[i][8]==UP && ADX[i][10]==UP) || (ADX[i][9]==UP && ADX[i][10]==UP))
         ADX_consensus = UP;
      else if((ADX[i][8]==DOWN && ADX[i][9]==DOWN) || (ADX[i][8]==DOWN && ADX[i][10]==DOWN) || (ADX[i][9]==DOWN && ADX[i][10]==DOWN))
         ADX_consensus = DOWN;
      SetPanel("Row5_Label", 0, x_axis, baseY+(5*21), 80, 19, (ADX_consensus==UP) ? BullColor : ((ADX_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int PC_consensus = NONE;
      if((PC[i][8]==UP && PC[i][9]==UP) || (PC[i][8]==UP && PC[i][10]==UP) || (PC[i][9]==UP && PC[i][10]==UP))
         PC_consensus = UP;
      else if((PC[i][8]==DOWN && PC[i][9]==DOWN) || (PC[i][8]==DOWN && PC[i][10]==DOWN) || (PC[i][9]==DOWN && PC[i][10]==DOWN))
         PC_consensus = DOWN;
      SetPanel("Row6_Label", 0, x_axis, baseY+(6*21), 80, 19, (PC_consensus==UP) ? BullColor : ((PC_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int PK_consensus = NONE;
      if((PK[i][8]==UP && PK[i][9]==UP) || (PK[i][8]==UP && PK[i][10]==UP) || (PK[i][9]==UP && PK[i][10]==UP))
         PK_consensus = UP;
      else if((PK[i][8]==DOWN && PK[i][9]==DOWN) || (PK[i][8]==DOWN && PK[i][10]==DOWN) || (PK[i][9]==DOWN && PK[i][10]==DOWN))
         PK_consensus = DOWN;
      SetPanel("Row7_Label", 0, x_axis, baseY+(7*21), 80, 19, (PK_consensus==UP) ? BullColor : ((PK_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int PT_consensus = NONE;
      if((PT[i][8]==UP && PT[i][9]==UP) || (PT[i][8]==UP && PT[i][10]==UP) || (PT[i][9]==UP && PT[i][10]==UP))
         PT_consensus = UP;
      else if((PT[i][8]==DOWN && PT[i][9]==DOWN) || (PT[i][8]==DOWN && PT[i][10]==DOWN) || (PT[i][9]==DOWN && PT[i][10]==DOWN))
         PT_consensus = DOWN;
      SetPanel("Row8_Label", 0, x_axis, baseY+(8*21), 80, 19, (PT_consensus==UP) ? BullColor : ((PT_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int PA_consensus = NONE;
      if((PA[i][8]==UP && PA[i][9]==UP) || (PA[i][8]==UP && PA[i][10]==UP) || (PA[i][9]==UP && PA[i][10]==UP))
         PA_consensus = UP;
      else if((PA[i][8]==DOWN && PA[i][9]==DOWN) || (PA[i][8]==DOWN && PA[i][10]==DOWN) || (PA[i][9]==DOWN && PA[i][10]==DOWN))
         PA_consensus = DOWN;
      SetPanel("Row9_Label", 0, x_axis, baseY+(9*21), 80, 19, (PA_consensus==UP) ? BullColor : ((PA_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int PB_consensus = NONE;
      if((PB[i][8]==UP && PB[i][9]==UP) || (PB[i][8]==UP && PB[i][10]==UP) || (PB[i][9]==UP && PB[i][10]==UP))
         PB_consensus = UP;
      else if((PB[i][8]==DOWN && PB[i][9]==DOWN) || (PB[i][8]==DOWN && PB[i][10]==DOWN) || (PB[i][9]==DOWN && PB[i][10]==DOWN))
         PB_consensus = DOWN;
      SetPanel("Row10_Label", 0, x_axis, baseY+(10*21), 80, 19, (PB_consensus==UP) ? BullColor : ((PB_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int TSlope_consensus = NONE;
      if((TSlope[i][8]==UP && TSlope[i][9]==UP) || (TSlope[i][8]==UP && TSlope[i][10]==UP) || (TSlope[i][9]==UP && TSlope[i][10]==UP))
         TSlope_consensus = UP;
      else if((TSlope[i][8]==DOWN && TSlope[i][9]==DOWN) || (TSlope[i][8]==DOWN && TSlope[i][10]==DOWN) || (TSlope[i][9]==DOWN && TSlope[i][10]==DOWN))
         TSlope_consensus = DOWN;
      SetPanel("Row11_Label", 0, x_axis, baseY+(11*21), 80, 19, (TSlope_consensus==UP) ? BullColor : ((TSlope_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int KSlope_consensus = NONE;
      if((KSlope[i][8]==UP && KSlope[i][9]==UP) || (KSlope[i][8]==UP && KSlope[i][10]==UP) || (KSlope[i][9]==UP && KSlope[i][10]==UP))
         KSlope_consensus = UP;
      else if((KSlope[i][8]==DOWN && KSlope[i][9]==DOWN) || (KSlope[i][8]==DOWN && KSlope[i][10]==DOWN) || (KSlope[i][9]==DOWN && KSlope[i][10]==DOWN))
         KSlope_consensus = DOWN;
      SetPanel("Row12_Label", 0, x_axis, baseY+(12*21), 80, 19, (KSlope_consensus==UP) ? BullColor : ((KSlope_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int TK_consensus = NONE;
      if((TK[i][8]==UP && TK[i][9]==UP) || (TK[i][8]==UP && TK[i][10]==UP) || (TK[i][9]==UP && TK[i][10]==UP))
         TK_consensus = UP;
      else if((TK[i][8]==DOWN && TK[i][9]==DOWN) || (TK[i][8]==DOWN && TK[i][10]==DOWN) || (TK[i][9]==DOWN && TK[i][10]==DOWN))
         TK_consensus = DOWN;
      SetPanel("Row13_Label", 0, x_axis, baseY+(13*21), 80, 19, (TK_consensus==UP) ? BullColor : ((TK_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int TC_consensus = NONE;
      if((TC[i][8]==UP && TC[i][9]==UP) || (TC[i][8]==UP && TC[i][10]==UP) || (TC[i][9]==UP && TC[i][10]==UP))
         TC_consensus = UP;
      else if((TC[i][8]==DOWN && TC[i][9]==DOWN) || (TC[i][8]==DOWN && TC[i][10]==DOWN) || (TC[i][9]==DOWN && TC[i][10]==DOWN))
         TC_consensus = DOWN;
      SetPanel("Row14_Label", 0, x_axis, baseY+(14*21), 80, 19, (TC_consensus==UP) ? BullColor : ((TC_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int KC_consensus = NONE;
      if((KC[i][8]==UP && KC[i][9]==UP) || (KC[i][8]==UP && KC[i][10]==UP) || (KC[i][9]==UP && KC[i][10]==UP))
         KC_consensus = UP;
      else if((KC[i][8]==DOWN && KC[i][9]==DOWN) || (KC[i][8]==DOWN && KC[i][10]==DOWN) || (KC[i][9]==DOWN && KC[i][10]==DOWN))
         KC_consensus = DOWN;
      SetPanel("Row15_Label", 0, x_axis, baseY+(15*21), 80, 19, (KC_consensus==UP) ? BullColor : ((KC_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int Kumo_consensus = NONE;
      if((Kumo[i][8]==UP && Kumo[i][9]==UP) || (Kumo[i][8]==UP && Kumo[i][10]==UP) || (Kumo[i][9]==UP && Kumo[i][10]==UP))
         Kumo_consensus = UP;
      else if((Kumo[i][8]==DOWN && Kumo[i][9]==DOWN) || (Kumo[i][8]==DOWN && Kumo[i][10]==DOWN) || (Kumo[i][9]==DOWN && Kumo[i][10]==DOWN))
         Kumo_consensus = DOWN;
      SetPanel("Row16_Label", 0, x_axis, baseY+(16*21), 80, 19, (Kumo_consensus==UP) ? BullColor : ((Kumo_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int DeltaThick_consensus = NONE;
      if((DeltaThick[i][8]==UP && DeltaThick[i][9]==UP) || (DeltaThick[i][8]==UP && DeltaThick[i][10]==UP) || (DeltaThick[i][9]==UP && DeltaThick[i][10]==UP))
         DeltaThick_consensus = UP;
      else if((DeltaThick[i][8]==DOWN && DeltaThick[i][9]==DOWN) || (DeltaThick[i][8]==DOWN && DeltaThick[i][10]==DOWN) || (DeltaThick[i][9]==DOWN && DeltaThick[i][10]==DOWN))
         DeltaThick_consensus = DOWN;
      SetPanel("Row17_Label", 0, x_axis, baseY+(17*21), 80, 19, (DeltaThick_consensus==UP) ? BullColor : ((DeltaThick_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int ChP_consensus = NONE;
      if((ChP[i][8]==UP && ChP[i][9]==UP) || (ChP[i][8]==UP && ChP[i][10]==UP) || (ChP[i][9]==UP && ChP[i][10]==UP))
         ChP_consensus = UP;
      else if((ChP[i][8]==DOWN && ChP[i][9]==DOWN) || (ChP[i][8]==DOWN && ChP[i][10]==DOWN) || (ChP[i][9]==DOWN && ChP[i][10]==DOWN))
         ChP_consensus = DOWN;
      SetPanel("Row18_Label", 0, x_axis, baseY+(18*21), 80, 19, (ChP_consensus==UP) ? BullColor : ((ChP_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int ChC_consensus = NONE;
      if((ChC[i][8]==UP && ChC[i][9]==UP) || (ChC[i][8]==UP && ChC[i][10]==UP) || (ChC[i][9]==UP && ChC[i][10]==UP))
         ChC_consensus = UP;
      else if((ChC[i][8]==DOWN && ChC[i][9]==DOWN) || (ChC[i][8]==DOWN && ChC[i][10]==DOWN) || (ChC[i][9]==DOWN && ChC[i][10]==DOWN))
         ChC_consensus = DOWN;
      SetPanel("Row19_Label", 0, x_axis, baseY+(19*21), 80, 19, (ChC_consensus==UP) ? BullColor : ((ChC_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int Twist_consensus = NONE;
      if((Twist[i][8]==UP && Twist[i][9]==UP) || (Twist[i][8]==UP && Twist[i][10]==UP) || (Twist[i][9]==UP && Twist[i][10]==UP))
         Twist_consensus = UP;
      else if((Twist[i][8]==DOWN && Twist[i][9]==DOWN) || (Twist[i][8]==DOWN && Twist[i][10]==DOWN) || (Twist[i][9]==DOWN && Twist[i][10]==DOWN))
         Twist_consensus = DOWN;
      SetPanel("Row20_Label", 0, x_axis, baseY+(20*21), 80, 19, (Twist_consensus==UP) ? BullColor : ((Twist_consensus==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      
      int Thick_consensus = NONE;
      if((Thick[i][8]==UP && Thick[i][9]==UP) || (Thick[i][8]==UP && Thick[i][10]==UP) || (Thick[i][9]==UP && Thick[i][10]==UP))
         Thick_consensus = UP;
      SetPanel("Row21_Label", 0, x_axis, baseY+(21*21), 80, 19, (Thick_consensus==UP) ? clrYellow : Box_Color, BoarderColor, 1);
      
      int ChFree_consensus = NONE;
      if((ChFree[i][8]==UP && ChFree[i][9]==UP) || (ChFree[i][8]==UP && ChFree[i][10]==UP) || (ChFree[i][9]==UP && ChFree[i][10]==UP))
         ChFree_consensus = UP;
      SetPanel("Row22_Label", 0, x_axis, baseY+(22*21), 80, 19, (ChFree_consensus==UP) ? clrYellow : Box_Color, BoarderColor, 1);
      
      SetPanel("Row23_Label", 0, x_axis, baseY+(23*21), 80, 19, Box_Color, BoarderColor, 1);
      SetPanel("Row24_Label", 0, x_axis, baseY+(24*21), 80, 19, Box_Color, BoarderColor, 1);
      SetPanel("Row25_Label", 0, x_axis, baseY+(25*21), 80, 19, Box_Color, BoarderColor, 1);
      SetPanel("Row26_Label", 0, x_axis, baseY+(26*21), 80, 19, Box_Color, BoarderColor, 1);
      
      m1_box = (PK[i][0]==UP && SAR[i][0]==UP && MACD[i][0]==UP && RSI[i][0]==UP && ADX[i][0]==UP) ? UP : 
               ((PK[i][0]==DOWN && SAR[i][0]==DOWN && MACD[i][0]==DOWN && RSI[i][0]==DOWN && ADX[i][0]==DOWN) ? DOWN : NONE);
      m5_box = (PK[i][1]==UP && SAR[i][1]==UP && MACD[i][1]==UP && RSI[i][1]==UP && ADX[i][1]==UP) ? UP : 
               ((PK[i][1]==DOWN && SAR[i][1]==DOWN && MACD[i][1]==DOWN && RSI[i][1]==DOWN && ADX[i][1]==DOWN) ? DOWN : NONE);
      m15_box = (PK[i][2]==UP && SAR[i][2]==UP && MACD[i][2]==UP && RSI[i][2]==UP && ADX[i][2]==UP) ? UP : 
                ((PK[i][2]==DOWN && SAR[i][2]==DOWN && MACD[i][2]==DOWN && RSI[i][2]==DOWN && ADX[i][2]==DOWN) ? DOWN : NONE);
      h1_box = (PK[i][3]==UP && SAR[i][3]==UP && MACD[i][3]==UP && RSI[i][3]==UP && ADX[i][3]==UP) ? UP : 
               ((PK[i][3]==DOWN && SAR[i][3]==DOWN && MACD[i][3]==DOWN && RSI[i][3]==DOWN && ADX[i][3]==DOWN) ? DOWN : NONE);
      h4_box = (PK[i][4]==UP && SAR[i][4]==UP && MACD[i][4]==UP && RSI[i][4]==UP && ADX[i][4]==UP) ? UP : 
               ((PK[i][4]==DOWN && SAR[i][4]==DOWN && MACD[i][4]==DOWN && RSI[i][4]==DOWN && ADX[i][4]==DOWN) ? DOWN : NONE);
      d1_box = (PK[i][5]==UP && SAR[i][5]==UP && MACD[i][5]==UP && RSI[i][5]==UP && ADX[i][5]==UP) ? UP : 
               ((PK[i][5]==DOWN && SAR[i][5]==DOWN && MACD[i][5]==DOWN && RSI[i][5]==DOWN && ADX[i][5]==DOWN) ? DOWN : NONE);
      w1_box = (PK[i][6]==UP && SAR[i][6]==UP && MACD[i][6]==UP && RSI[i][6]==UP && ADX[i][6]==UP) ? UP : 
               ((PK[i][6]==DOWN && SAR[i][6]==DOWN && MACD[i][6]==DOWN && RSI[i][6]==DOWN && ADX[i][6]==DOWN) ? DOWN : NONE);
      mn_box = (PK[i][7]==UP && SAR[i][7]==UP && MACD[i][7]==UP && RSI[i][7]==UP && ADX[i][7]==UP) ? UP : 
               ((PK[i][7]==DOWN && SAR[i][7]==DOWN && MACD[i][7]==DOWN && RSI[i][7]==DOWN && ADX[i][7]==DOWN) ? DOWN : NONE);
      
      if(i == 0)
      {
         SetPanel("TF_Box_M1", 0, x_axis+83, baseY-21, 19, 19, (m1_box==UP) ? BullColor : ((m1_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
         SetPanel("TF_Box_M5", 0, x_axis+102, baseY-21, 19, 19, (m5_box==UP) ? BullColor : ((m5_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
         SetPanel("TF_Box_M15", 0, x_axis+121, baseY-21, 19, 19, (m15_box==UP) ? BullColor : ((m15_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
         SetPanel("TF_Box_H1", 0, x_axis+140, baseY-21, 19, 19, (h1_box==UP) ? BullColor : ((h1_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
         SetPanel("TF_Box_H4", 0, x_axis+159, baseY-21, 19, 19, (h4_box==UP) ? BullColor : ((h4_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
         SetPanel("TF_Box_D1", 0, x_axis+178, baseY-21, 19, 19, (d1_box==UP) ? BullColor : ((d1_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
         SetPanel("TF_Box_W1", 0, x_axis+197, baseY-21, 19, 19, (w1_box==UP) ? BullColor : ((w1_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
         SetPanel("TF_Box_MN", 0, x_axis+216, baseY-21, 19, 19, (mn_box==UP) ? BullColor : ((mn_box==DOWN) ? BearColor : Box_Color), BoarderColor, 1);
      }
      
      ShortTerm_trend = (PK[i][8]==UP && SAR[i][8]==UP && MACD[i][8]==UP && RSI[i][8]==UP && ADX[i][8]==UP) ? UP : 
                        ((PK[i][8]==DOWN && SAR[i][8]==DOWN && MACD[i][8]==DOWN && RSI[i][8]==DOWN && ADX[i][8]==DOWN) ? DOWN : NONE);
      MediumTerm_trend = (PK[i][9]==UP && SAR[i][9]==UP && MACD[i][9]==UP && RSI[i][9]==UP && ADX[i][9]==UP) ? UP : 
                         ((PK[i][9]==DOWN && SAR[i][9]==DOWN && MACD[i][9]==DOWN && RSI[i][9]==DOWN && ADX[i][9]==DOWN) ? DOWN : NONE);
      LongTerm_trend = (PK[i][10]==UP && SAR[i][10]==UP && MACD[i][10]==UP && RSI[i][10]==UP && ADX[i][10]==UP) ? UP : 
                       ((PK[i][10]==DOWN && SAR[i][10]==DOWN && MACD[i][10]==DOWN && RSI[i][10]==DOWN && ADX[i][10]==DOWN) ? DOWN : NONE);
      
      if(i == 0)
      {
         SetPanel("GMMA_Box_S", 0, x_axis+239, baseY-21, 19, 19, (ShortTerm_trend==UP) ? LightBullColor : ((ShortTerm_trend==DOWN) ? LightBearColor : Box_Color), BoarderColor, 1);
         SetPanel("GMMA_Box_M", 0, x_axis+258, baseY-21, 19, 19, (MediumTerm_trend==UP) ? LightBullColor : ((MediumTerm_trend==DOWN) ? LightBearColor : Box_Color), BoarderColor, 1);
         SetPanel("GMMA_Box_L", 0, x_axis+277, baseY-21, 19, 19, (LongTerm_trend==UP) ? LightBullColor : ((LongTerm_trend==DOWN) ? LightBearColor : Box_Color), BoarderColor, 1);
      }
   }
}

//+------------------------------------------------------------------+
//| Check if Timeframe Consensus Matches Expected Status             |
//| Used for arrow indicator background highlighting                 |
//| Returns: true if timeframe box matches specified direction       |
//+------------------------------------------------------------------+
bool ConditionCheck(int i, int j, int status)
{
   switch(j)
   {
      case 0: return (m1_box == status);
      case 1: return (m5_box == status);
      case 2: return (m15_box == status);
      case 3: return (h1_box == status);
      case 4: return (h4_box == status);
      case 5: return (d1_box == status);
      case 6: return (w1_box == status);
      case 7: return (mn_box == status);
   }
   return false;
}

//+------------------------------------------------------------------+
//| Display Tenkan/Cloud Breakout Arrows                             |
//| Renders upward/downward arrows when TC breakout detected         |
//| Highlights cell background if overall timeframe is aligned       |
//| Orange color indicates Tenkan-specific signal                    |
//+------------------------------------------------------------------+
void Display_TC_Arrows()
{
   int baseY = y_axis + 107;
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         int xPos = x_axis + 87 + (j * 19);
         int yPos = baseY + (23 * 21) + 3;
         
         ObjectDelete(0, "TC_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j));
         
         if(TC_Arrow[i][j] == UP)
         {
            SetObjText("TC_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(233), xPos, yPos, clrOrange, 10);
            if(ConditionCheck(i, j, UP)) ColorPanel("Indicator_R23_TF"+IntegerToString(j), clrOrange, BoarderColor);
         }
         else if(TC_Arrow[i][j] == DOWN)
         {
            SetObjText("TC_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(234), xPos, yPos, clrOrange, 10);
            if(ConditionCheck(i, j, DOWN)) ColorPanel("Indicator_R23_TF"+IntegerToString(j), clrOrange, BoarderColor);
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Display Kijun/Cloud Breakout Arrows                              |
//| Renders upward/downward arrows when KC breakout detected         |
//| Uses bull/bear colors to match signal direction                  |
//| Highlights cell background if overall timeframe is aligned       |
//+------------------------------------------------------------------+
void Display_KC_Arrows()
{
   int baseY = y_axis + 107;
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         int xPos = x_axis + 87 + (j * 19);
         int yPos = baseY + (24 * 21) + 3;
         
         ObjectDelete(0, "KC_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j));
         
         if(KC_Arrow[i][j] == UP)
         {
            SetObjText("KC_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(233), xPos, yPos, LightBullColor, 10);
            if(ConditionCheck(i, j, UP)) ColorPanel("Indicator_R24_TF"+IntegerToString(j), LightBullColor, BoarderColor);
         }
         else if(KC_Arrow[i][j] == DOWN)
         {
            SetObjText("KC_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(234), xPos, yPos, LightBearColor, 10);
            if(ConditionCheck(i, j, DOWN)) ColorPanel("Indicator_R24_TF"+IntegerToString(j), LightBearColor, BoarderColor);
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Display Cloud Twist Arrows                                       |
//| Shows diamond arrows when future cloud changes color             |
//| Gold color indicates leading nature of this signal               |
//| Highlights cell background if overall timeframe is aligned       |
//+------------------------------------------------------------------+
void Display_Twist_Arrows()
{
   int baseY = y_axis + 107;
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         int xPos = x_axis + 87 + (j * 19);
         int yPos = baseY + (25 * 21) + 3;
         
         ObjectDelete(0, "Twist_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j));
         
         if(Twist_Arrow[i][j] == UP)
         {
            SetObjText("Twist_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(225), xPos, yPos, clrGold, 11);
            if(ConditionCheck(i, j, UP)) ColorPanel("Indicator_R25_TF"+IntegerToString(j), clrGold, BoarderColor);
         }
         else if(Twist_Arrow[i][j] == DOWN)
         {
            SetObjText("Twist_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(226), xPos, yPos, clrGold, 11);
            if(ConditionCheck(i, j, DOWN)) ColorPanel("Indicator_R25_TF"+IntegerToString(j), clrGold, BoarderColor);
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Display MACD Crossover Arrows                                    |
//| Renders arrows when MACD crosses signal line in trending zone    |
//| Uses bull/bear colors to match signal direction                  |
//| Highlights cell background if overall timeframe is aligned       |
//+------------------------------------------------------------------+
void Display_MACD_Arrows()
{
   int baseY = y_axis + 107;
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         int xPos = x_axis + 87 + (j * 19);
         int yPos = baseY + (26 * 21) + 3;
         
         ObjectDelete(0, "MACD_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j));
         
         if(MACD_Arrow[i][j] == UP)
         {
            SetObjText("MACD_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(233), xPos, yPos, LightBullColor, 10);
            if(ConditionCheck(i, j, UP)) ColorPanel("Indicator_R26_TF"+IntegerToString(j), LightBullColor, BoarderColor);
         }
         else if(MACD_Arrow[i][j] == DOWN)
         {
            SetObjText("MACD_Arrow_"+IntegerToString(i)+"_"+IntegerToString(j), CharToStr(234), xPos, yPos, LightBearColor, 10);
            if(ConditionCheck(i, j, DOWN)) ColorPanel("Indicator_R26_TF"+IntegerToString(j), LightBearColor, BoarderColor);
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Calculate Daily ATR Percentage                                   |
//| Formula: (Today's Range / Daily ATR) x 100                       |
//| Indicates how much of average daily range has been consumed      |
//| Used for exhaustion filtering in probability calculation          |
//| Low values: early in move, high values: extended/exhausted       |
//+------------------------------------------------------------------+
double Calculate_Daily_ATR_Percent(int pair_idx)
{
   double daily_range = pairinfo[pair_idx].dayHigh - pairinfo[pair_idx].dayLow;
   
   double daily_atr_raw = iATR(TradePairs[pair_idx], PERIOD_D1, ATR_Period, 0);
   
   if(daily_atr_raw <= 0) return 0;
   
   double atr_percent = (daily_range / daily_atr_raw) * 100.0;
   
   return atr_percent;
}

//+------------------------------------------------------------------+
//| Get Full Timeframe Name for Alert Messages                       |
//| Converts array index to human-readable timeframe string          |
//+------------------------------------------------------------------+
string GetTimeframeName(int tf_index)
{
   switch(tf_index)
   {
      case 0: return "M1";
      case 1: return "M5";
      case 2: return "M15";
      case 3: return "H1";
      case 4: return "H4";
      case 5: return "D1";
      case 6: return "W1";
      case 7: return "MN";
      default: return "??";
   }
}

//+------------------------------------------------------------------+
//| Tier 1 Confirmation Check for Arrow Alerts                       |
//| Implements multi-timeframe confirmation to reduce false signals  |
//| Logic:                                                           |
//| - D1/W1/MN arrows: instant alerts (no filtering required)        |
//| - Lower TFs: require alignment on 1-2 higher timeframes          |
//| - M1 arrow needs M5+M15 confirmation                             |
//| - M5 arrow needs M15+H1 confirmation                             |
//| - H1 arrow needs H4 confirmation                                 |
//| Checks 10 core indicators on confirmation timeframes             |
//| Returns: true if all required confirmations present              |
//+------------------------------------------------------------------+
bool Check_Tier1_Confirmation(int pair_idx, int arrow_tf_idx, int direction)
{
   if(arrow_tf_idx >= 5) return true;
   
   int conf_tf_1 = -1, conf_tf_2 = -1;
   
   switch(arrow_tf_idx)
   {
      case 0:
         conf_tf_1 = 1;
         conf_tf_2 = 2;
         break;
      case 1:
         conf_tf_1 = 2;
         conf_tf_2 = 3;
         break;
      case 2:
         conf_tf_1 = 3;
         conf_tf_2 = 4;
         break;
      case 3:
         conf_tf_1 = 4;
         conf_tf_2 = -1;
         break;
      case 4:
         conf_tf_1 = 5;
         conf_tf_2 = -1;
         break;
      default:
         return true;
   }
   
   int tier1_match_count = 0;
   int tier1_total = 10;
   
   if(conf_tf_1 >= 0)
   {
      if(Check_Indicator_Match(MACD[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(SAR[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PC[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PT[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PK[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PA[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PB[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(Twist[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(TC[pair_idx][conf_tf_1], direction)) tier1_match_count++;
      if(Check_Indicator_Match(KC[pair_idx][conf_tf_1], direction)) tier1_match_count++;
   }
   
   if(conf_tf_2 >= 0)
   {
      tier1_total = 20;
      
      if(Check_Indicator_Match(MACD[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(SAR[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PC[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PT[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PK[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PA[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(PB[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(Twist[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(TC[pair_idx][conf_tf_2], direction)) tier1_match_count++;
      if(Check_Indicator_Match(KC[pair_idx][conf_tf_2], direction)) tier1_match_count++;
   }
   
   if(tier1_match_count >= tier1_total)
      return true;
   
   return false;
}

//+------------------------------------------------------------------+
//| Check if Indicator Matches Arrow Direction                       |
//| Returns true if:                                                 |
//| - Indicator direction equals arrow direction, OR                 |
//| - Indicator is NONE and NONE-matching is enabled                 |
//| Used in confirmation logic to determine indicator agreement      |
//+------------------------------------------------------------------+
bool Check_Indicator_Match(int indicator_value, int arrow_direction)
{
   if(indicator_value == arrow_direction) return true;
   
   if(Allow_NONE_As_Match && indicator_value == NONE) return true;
   
   return false;
}

//+------------------------------------------------------------------+
//| Main Alert Processing System                                     |
//| Monitors all four arrow types across all pairs and timeframes    |
//| Implements state machine to prevent duplicate alerts             |
//| Processing flow for each arrow type:                             |
//| 1. Check if arrow signal is active and not previously alerted    |
//| 2. Verify Tier 1 confirmation from higher timeframes             |
//| 3. Dispatch alert with full market context                       |
//| 4. Update alert state flags to prevent re-alerting               |
//| 5. Reset flags when arrow signal disappears                      |
//+------------------------------------------------------------------+
void Check_And_Send_Arrow_Alerts()
{
   for(int i = 0; i < ArraySize(TradePairs); i++)
   {
      for(int j = 0; j < 8; j++)
      {
         if(TC_Arrow[i][j] == UP && !is_TC_up[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, UP))
            {
               Send_Arrow_Alert(i, j, UP, "T/Cloud");
               is_TC_up[i][j] = true;
               is_TC_down[i][j] = false;
            }
         }
         else if(TC_Arrow[i][j] == DOWN && !is_TC_down[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, DOWN))
            {
               Send_Arrow_Alert(i, j, DOWN, "T/Cloud");
               is_TC_down[i][j] = true;
               is_TC_up[i][j] = false;
            }
         }
         else if(TC_Arrow[i][j] == NONE)
         {
            is_TC_up[i][j] = false;
            is_TC_down[i][j] = false;
         }
         
         if(KC_Arrow[i][j] == UP && !is_KC_up[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, UP))
            {
               Send_Arrow_Alert(i, j, UP, "K/Cloud");
               is_KC_up[i][j] = true;
               is_KC_down[i][j] = false;
            }
         }
         else if(KC_Arrow[i][j] == DOWN && !is_KC_down[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, DOWN))
            {
               Send_Arrow_Alert(i, j, DOWN, "K/Cloud");
               is_KC_down[i][j] = true;
               is_KC_up[i][j] = false;
            }
         }
         else if(KC_Arrow[i][j] == NONE)
         {
            is_KC_up[i][j] = false;
            is_KC_down[i][j] = false;
         }
         
         if(Twist_Arrow[i][j] == UP && !is_Twist_up[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, UP))
            {
               Send_Arrow_Alert(i, j, UP, "Cloud Switch");
               is_Twist_up[i][j] = true;
               is_Twist_down[i][j] = false;
            }
         }
         else if(Twist_Arrow[i][j] == DOWN && !is_Twist_down[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, DOWN))
            {
               Send_Arrow_Alert(i, j, DOWN, "Cloud Switch");
               is_Twist_down[i][j] = true;
               is_Twist_up[i][j] = false;
            }
         }
         else if(Twist_Arrow[i][j] == NONE)
         {
            is_Twist_up[i][j] = false;
            is_Twist_down[i][j] = false;
         }
         
         if(MACD_Arrow[i][j] == UP && !is_MACD_up[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, UP))
            {
               Send_Arrow_Alert(i, j, UP, "MACD");
               is_MACD_up[i][j] = true;
               is_MACD_down[i][j] = false;
            }
         }
         else if(MACD_Arrow[i][j] == DOWN && !is_MACD_down[i][j])
         {
            if(Check_Tier1_Confirmation(i, j, DOWN))
            {
               Send_Arrow_Alert(i, j, DOWN, "MACD");
               is_MACD_down[i][j] = true;
               is_MACD_up[i][j] = false;
            }
         }
         else if(MACD_Arrow[i][j] == NONE)
         {
            is_MACD_up[i][j] = false;
            is_MACD_down[i][j] = false;
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Dispatch Filtered Alert with Complete Market Context             |
//| Pre-flight checks:                                               |
//| 1. Verify arrow type is enabled in settings                      |
//| 2. Verify timeframe is enabled in settings                       |
//| Alert content includes:                                          |
//| - Symbol, timeframe, and direction                               |
//| - Signal type that triggered alert                               |
//| - Current price and probability score                            |
//| - ATR percentage and timeframe-specific ATR                      |
//| - Confirmation timeframes used                                   |
//| - Short/Medium/Long term trend status                            |
//| Outputs to: terminal alerts, sound, push notifications, log      |
//+------------------------------------------------------------------+
void Send_Arrow_Alert(int pair_idx, int tf_idx, int direction, string arrow_type)
{
   if(arrow_type == "T/Cloud" && !Alert_TC_Arrow) return;
   if(arrow_type == "K/Cloud" && !Alert_KC_Arrow) return;
   if(arrow_type == "Cloud Switch" && !Alert_Twist_Arrow) return;
   if(arrow_type == "MACD" && !Alert_MACD_Arrow) return;
   
   bool tf_enabled = false;
   switch(tf_idx)
   {
      case 0: tf_enabled = Alert_M1; break;
      case 1: tf_enabled = Alert_M5; break;
      case 2: tf_enabled = Alert_M15; break;
      case 3: tf_enabled = Alert_H1; break;
      case 4: tf_enabled = Alert_H4; break;
      case 5: tf_enabled = Alert_D1; break;
      case 6: tf_enabled = Alert_W1; break;
      case 7: tf_enabled = Alert_MN; break;
   }
   
   if(!tf_enabled) return;
   
   string pair = TradePairs[pair_idx];
   string tf_name = GetTimeframeName(tf_idx);
   string dir_text = (direction == UP) ? "LONG" : "SHORT";
   
   double daily_atr_pct = Calculate_Daily_ATR_Percent(pair_idx);
   double prob = pairinfo[pair_idx].Trading_Prob;
   
   int timeframe = NumberToTimeframe(tf_idx);
   double atr_pips = iATR(pair, timeframe, ATR_Period, 0) / pairinfo[pair_idx].PairPip / pairinfo[pair_idx].pipsfactor;
   
   double current_price = iClose(pair, PERIOD_CURRENT, 0);
   int digits = (int)MarketInfo(pair, MODE_DIGITS);
   
   string conf_text = "";
   if(tf_idx == 0) conf_text = "M5+M15";
   else if(tf_idx == 1) conf_text = "M15+H1";
   else if(tf_idx == 2) conf_text = "H1+H4";
   else if(tf_idx == 3) conf_text = "H4";
   else if(tf_idx == 4) conf_text = "D1";
   else conf_text = "None";
   
   string s_trend = (ShortTerm_trend == UP) ? "BULL" : ((ShortTerm_trend == DOWN) ? "BEAR" : "NEUT");
   string m_trend = (MediumTerm_trend == UP) ? "BULL" : ((MediumTerm_trend == DOWN) ? "BEAR" : "NEUT");
   string l_trend = (LongTerm_trend == UP) ? "BULL" : ((LongTerm_trend == DOWN) ? "BEAR" : "NEUT");
   
   string full_msg = pair + " " + tf_name + " " + dir_text + "\n" +
                     "Signal: " + arrow_type + "\n" +
                     "Price: " + DoubleToStr(current_price, digits) + "\n" +
                     "Prob: " + DoubleToStr(prob, 2) + "\n" +
                     "ATR%: " + IntegerToString((int)MathRound(daily_atr_pct)) + "%\n" +
                     "ATR(" + tf_name + "): " + DoubleToStr(atr_pips, 1) + " pips\n" +
                     "Confirmed: " + conf_text + "\n" +
                     "Trend S/M/L: " + s_trend + "/" + m_trend + "/" + l_trend;
   
   if(SoundAlert)
   {
      Alert(full_msg);
      PlaySound("alert2.wav");
   }
   
   if(Sendnotification)
   {
      SendNotification(full_msg);
   }
   
   Print("=========================================");
   Print("ARROW ALERT: ", pair, " ", tf_name, " ", dir_text);
   Print("=========================================");
   Print("Signal Type: ", arrow_type);
   Print("Current Price: ", current_price);
   Print("Probability: ", prob);
   Print("ATR%: ", daily_atr_pct, "%");
   Print("ATR(", tf_name, "): ", atr_pips, " pips");
   Print("Confirmation: ", conf_text);
   Print("Trends - Short: ", s_trend, " | Med: ", m_trend, " | Long: ", l_trend);
   Print("Range Today: ", pairinfo[pair_idx].dayHigh - pairinfo[pair_idx].dayLow);
   Print("Daily ATR: ", pairinfo[pair_idx].atr);
   Print("=========================================");
}

//+------------------------------------------------------------------+
//| Sigmoid Function for Probability Mapping                         |
//| Converts linear edge values to probabilistic range [0, 1]        |
//| Handles numerical overflow with boundary checks                  |
//+------------------------------------------------------------------+
double Sigmoid(double x) 
{ 
   if(x > 20.0) return 1.0;
   if(x < -20.0) return 0.0;
   return 1.0 / (1.0 + MathExp(-x)); 
}

//+------------------------------------------------------------------+
//| Clamp Value to Specified Range                                   |
//| Ensures value remains within defined boundaries                  |
//+------------------------------------------------------------------+
double Clamp(double x, double lo, double hi)
{
   if(x < lo) return lo;
   if(x > hi) return hi;
   return x;
}

//+------------------------------------------------------------------+
//| Global Arrays for Probability Components                         |
//| P_Bull_Kelly: probability in [0,1] for Kelly Criterion           |
//| Confidence_Score: signal reliability metric in [0,1]             |
//+------------------------------------------------------------------+
double P_Bull_Kelly[];
double Confidence_Score[];

//+------------------------------------------------------------------+
//| Calculate Quantitative Trading Probability Score                 |
//| Multi-stage algorithm:                                           |
//| 1. Weighted aggregation of 19 directional indicators             |
//| 2. Multi-timeframe weighting (higher TF = higher weight)         |
//| 3. Confidence calculation from agreement, MTF alignment, regime  |
//| 4. Edge-to-probability conversion via calibrated sigmoid         |
//| 5. Asymmetric ATR% filter application                            |
//| 6. Exponential smoothing to reduce noise                         |
//| Output: Trading_Prob in [-1, +1] stored in pairinfo structure    |
//| Also populates P_Bull_Kelly and Confidence_Score for extensions  |
//+------------------------------------------------------------------+
void GetTradingProb()
{
   int numPairs = ArraySize(TradePairs);
   
   if(ArraySize(P_Bull_Kelly) != numPairs)
   {
      ArrayResize(P_Bull_Kelly, numPairs);
      ArrayResize(Confidence_Score, numPairs);
   }
   
   static double prev_scores[];
   static bool score_initialized[];
   
   if(ArraySize(prev_scores) != numPairs)
   {
      ArrayResize(prev_scores, numPairs);
      ArrayResize(score_initialized, numPairs);
      for(int k = 0; k < numPairs; k++) score_initialized[k] = false;
   }
   
   for(int i = 0; i < numPairs; i++)
   {
      double W_TF[8];
      W_TF[0] = 1.0;
      W_TF[1] = 1.5;
      W_TF[2] = 2.0;
      W_TF[3] = 3.0;
      W_TF[4] = 5.0;
      W_TF[5] = 8.0;
      W_TF[6] = 10.0;
      W_TF[7] = 12.0;
      
      double W_PC = 4.0;
      double W_PK = 3.5;
      double W_Kumo = 3.0;
      double W_Twist = 3.0;
      double W_ChC = 2.5;
      double W_TC = 2.0;
      double W_KC = 2.5;
      double W_TK = 2.0;
      double W_ChP = 2.0;
      double W_PA = 1.5;
      double W_PB = 1.5;
      double W_PT = 1.5;
      double W_MACD = 2.5;
      double W_ADX = 2.0;
      double W_SAR = 2.0;
      double W_RSI = 1.5;
      double W_TSlope = 1.5;
      double W_KSlope = 1.5;
      double W_DeltaThick = 1.0;
      
      double total_ind_weight = W_PC + W_PK + W_Kumo + W_Twist + W_ChC +
                                W_TC + W_KC + W_TK + W_ChP + W_PA + W_PB + W_PT +
                                W_MACD + W_ADX + W_SAR + W_RSI +
                                W_TSlope + W_KSlope + W_DeltaThick;
      
      double weighted_sum = 0.0;
      double weight_total = 0.0;
      double active_weight_total = 0.0;
      
      for(int j = 0; j < 8; j++)
      {
         double tf_w = W_TF[j];
         double tf_sum = 0.0;
         double tf_active = 0.0;
         
         tf_sum += PC[i][j] * W_PC;
         if(PC[i][j] != NONE) tf_active += W_PC;
         
         tf_sum += PK[i][j] * W_PK;
         if(PK[i][j] != NONE) tf_active += W_PK;
         
         tf_sum += Kumo[i][j] * W_Kumo;
         if(Kumo[i][j] != NONE) tf_active += W_Kumo;
         
         tf_sum += Twist[i][j] * W_Twist;
         if(Twist[i][j] != NONE) tf_active += W_Twist;
         
         tf_sum += ChC[i][j] * W_ChC;
         if(ChC[i][j] != NONE) tf_active += W_ChC;
         
         tf_sum += TC[i][j] * W_TC;
         if(TC[i][j] != NONE) tf_active += W_TC;
         
         tf_sum += KC[i][j] * W_KC;
         if(KC[i][j] != NONE) tf_active += W_KC;
         
         tf_sum += TK[i][j] * W_TK;
         if(TK[i][j] != NONE) tf_active += W_TK;
         
         tf_sum += ChP[i][j] * W_ChP;
         if(ChP[i][j] != NONE) tf_active += W_ChP;
         
         tf_sum += PA[i][j] * W_PA;
         if(PA[i][j] != NONE) tf_active += W_PA;
         
         tf_sum += PB[i][j] * W_PB;
         if(PB[i][j] != NONE) tf_active += W_PB;
         
         tf_sum += PT[i][j] * W_PT;
         if(PT[i][j] != NONE) tf_active += W_PT;
         
         tf_sum += MACD[i][j] * W_MACD;
         if(MACD[i][j] != NONE) tf_active += W_MACD;
         
         tf_sum += ADX[i][j] * W_ADX;
         if(ADX[i][j] != NONE) tf_active += W_ADX;
         
         tf_sum += SAR[i][j] * W_SAR;
         if(SAR[i][j] != NONE) tf_active += W_SAR;
         
         tf_sum += RSI[i][j] * W_RSI;
         if(RSI[i][j] != NONE) tf_active += W_RSI;
         
         tf_sum += TSlope[i][j] * W_TSlope;
         if(TSlope[i][j] != NONE) tf_active += W_TSlope;
         
         tf_sum += KSlope[i][j] * W_KSlope;
         if(KSlope[i][j] != NONE) tf_active += W_KSlope;
         
         tf_sum += DeltaThick[i][j] * W_DeltaThick;
         if(DeltaThick[i][j] != NONE) tf_active += W_DeltaThick;
         
         weighted_sum += tf_sum * tf_w;
         weight_total += total_ind_weight * tf_w;
         active_weight_total += tf_active * tf_w;
      }
      
      double edge = (active_weight_total > 0) ? weighted_sum / active_weight_total : 0.0;
      edge = Clamp(edge, -1.0, 1.0);
      
      double c_agree = (active_weight_total > 0) ? 
                       MathAbs(weighted_sum) / active_weight_total : 0.0;
      c_agree = Clamp(c_agree, 0.0, 1.0);
      
      int dir = (edge > 0.05) ? UP : ((edge < -0.05) ? DOWN : NONE);
      double c_mtf = 0.5;
      
      if(dir != NONE)
      {
         int aligned = 0;
         
         int short_bull = 0, short_bear = 0;
         for(int j = 0; j <= 2; j++)
         {
            if(PC[i][j] == UP) short_bull++; else if(PC[i][j] == DOWN) short_bear++;
            if(PK[i][j] == UP) short_bull++; else if(PK[i][j] == DOWN) short_bear++;
            if(Kumo[i][j] == UP) short_bull++; else if(Kumo[i][j] == DOWN) short_bear++;
            if(MACD[i][j] == UP) short_bull++; else if(MACD[i][j] == DOWN) short_bear++;
         }
         int short_dir = (short_bull > short_bear + 3) ? UP : 
                         ((short_bear > short_bull + 3) ? DOWN : NONE);
         if(short_dir == dir) aligned++;
         
         int med_bull = 0, med_bear = 0;
         for(int j = 3; j <= 4; j++)
         {
            if(PC[i][j] == UP) med_bull++; else if(PC[i][j] == DOWN) med_bear++;
            if(PK[i][j] == UP) med_bull++; else if(PK[i][j] == DOWN) med_bear++;
            if(Kumo[i][j] == UP) med_bull++; else if(Kumo[i][j] == DOWN) med_bear++;
            if(MACD[i][j] == UP) med_bull++; else if(MACD[i][j] == DOWN) med_bear++;
         }
         int med_dir = (med_bull > med_bear + 2) ? UP : 
                       ((med_bear > med_bull + 2) ? DOWN : NONE);
         if(med_dir == dir) aligned++;
         
         int long_bull = 0, long_bear = 0;
         for(int j = 5; j <= 7; j++)
         {
            if(PC[i][j] == UP) long_bull++; else if(PC[i][j] == DOWN) long_bear++;
            if(PK[i][j] == UP) long_bull++; else if(PK[i][j] == DOWN) long_bear++;
            if(Kumo[i][j] == UP) long_bull++; else if(Kumo[i][j] == DOWN) long_bear++;
            if(Twist[i][j] == UP) long_bull++; else if(Twist[i][j] == DOWN) long_bear++;
         }
         int long_dir = (long_bull > long_bear + 3) ? UP : 
                        ((long_bear > long_bull + 3) ? DOWN : NONE);
         if(long_dir == dir) aligned++;
         
         c_mtf = aligned / 3.0;
      }
      
      double atr_score = 0.0;
      double adx_score = 0.0;
      double tf_weight_sum = 0.0;
      
      for(int j = 0; j < 8; j++)
      {
         tf_weight_sum += W_TF[j];
         if(ATR[i][j] == UP) atr_score += W_TF[j];
         
         double adx_main = iADX(TradePairs[i], NumberToTimeframe(j), ADX_period, PRICE_CLOSE, MODE_MAIN, 0);
         if(adx_main > ADX_strong_trend) adx_score += W_TF[j];
      }
      
      double c_atr = Clamp((atr_score / tf_weight_sum) / 0.60, 0.0, 1.0);
      double c_adx = adx_score / tf_weight_sum;
      double c_regime = 0.5 * c_atr + 0.5 * c_adx;
      
      double confidence = Clamp(
         0.45 * c_agree +
         0.35 * c_mtf +
         0.20 * c_regime,
         0.0, 1.0
      );
      
      Confidence_Score[i] = confidence;
      
      double P_bull_raw = Sigmoid(Beta_Calibration * edge);
      
      double P_bull_kelly = 0.5 + confidence * (P_bull_raw - 0.5);
      P_Bull_Kelly[i] = Clamp(P_bull_kelly, 0.0, 1.0);
      
      double signed_raw = 2.0 * P_bull_raw - 1.0;
      double signed_adjusted = confidence * signed_raw;
      
      double atr_pct = Calculate_Daily_ATR_Percent(i);
      int day_direction = GetDayDirection(i);
      int signal_direction = (signed_adjusted > 0.05) ? UP : 
                             ((signed_adjusted < -0.05) ? DOWN : NONE);
      
      double atr_factor = 1.0;
      
      if(Use_ATR_Percent_Filter)
      {
         bool is_trend_following = (signal_direction == day_direction && 
                                    signal_direction != NONE && 
                                    day_direction != NONE);
         
         bool is_counter_trend = (signal_direction != NONE && 
                                  day_direction != NONE && 
                                  signal_direction != day_direction);
         
         if(atr_pct <= ATR_Hunt_Zone_Max)
         {
            atr_factor = 1.0;
         }
         else if(atr_pct <= ATR_Caution_Zone)
         {
            if(is_trend_following)
            {
               double progress = (atr_pct - ATR_Hunt_Zone_Max) / (ATR_Caution_Zone - ATR_Hunt_Zone_Max);
               atr_factor = 1.0 - (0.5 * progress);
            }
            else if(is_counter_trend && Allow_CounterTrend_Above_75)
            {
               atr_factor = 1.0;
            }
            else
            {
               atr_factor = 0.8;
            }
         }
         else if(atr_pct <= ATR_Full_Suppress)
         {
            if(is_trend_following)
            {
               double progress = (atr_pct - ATR_Caution_Zone) / (ATR_Full_Suppress - ATR_Caution_Zone);
               atr_factor = 0.5 * (1.0 - progress);
            }
            else if(is_counter_trend && Allow_CounterTrend_Above_75)
            {
               atr_factor = CounterTrend_Boost;
            }
            else
            {
               atr_factor = 0.3;
            }
         }
         else
         {
            if(is_trend_following)
            {
               atr_factor = 0.0;
            }
            else if(is_counter_trend && Allow_CounterTrend_Above_75)
            {
               atr_factor = CounterTrend_Boost * 1.1;
            }
            else
            {
               atr_factor = 0.1;
            }
         }
      }
      
      double filtered_score = signed_adjusted * atr_factor;
      
      double smoothed;
      
      if(!score_initialized[i])
      {
         smoothed = filtered_score;
         score_initialized[i] = true;
      }
      else
      {
         smoothed = Smoothing_Factor * filtered_score + (1.0 - Smoothing_Factor) * prev_scores[i];
      }
      
      prev_scores[i] = smoothed;
      
      pairinfo[i].Trading_Prob = Clamp(smoothed, -1.0, 1.0);
   }
}

//+------------------------------------------------------------------+
//| Calculate Kelly Criterion Position Size                          |
//| Formula: f = (p*b - q) / b                                       |
//| Where: p = win probability, q = loss probability, b = R:R ratio  |
//| Returns: optimal position size as fraction of capital            |
//| Safety limits: max 25% allocation, scaled by confidence          |
//| Handles both long (p>0.5) and short (p<0.5) positions            |
//+------------------------------------------------------------------+
double Calculate_Kelly_Fraction(int pair_idx, double reward_risk_ratio)
{
   if(pair_idx < 0 || pair_idx >= ArraySize(P_Bull_Kelly)) return 0.0;
   
   double p = P_Bull_Kelly[pair_idx];
   double q = 1.0 - p;
   double b = reward_risk_ratio;
   
   if(b <= 0) return 0.0;
   
   double kelly;
   
   if(p > 0.5)
   {
      kelly = (p * b - q) / b;
   }
   else
   {
      double p_bear = 1.0 - p;
      double q_bear = p;
      kelly = (p_bear * b - q_bear) / b;
   }
   
   kelly = Clamp(kelly, 0.0, 0.25);
   
   if(pair_idx < ArraySize(Confidence_Score))
      kelly = kelly * Confidence_Score[pair_idx];
   
   return kelly;
}

//+------------------------------------------------------------------+
//| Retrieve Bullish Probability for External Access                 |
//| Returns: P(Bull) in range [0, 1] for specified pair              |
//+------------------------------------------------------------------+
double Get_P_Bull(int pair_idx)
{
   if(pair_idx < 0 || pair_idx >= ArraySize(P_Bull_Kelly)) return 0.5;
   return P_Bull_Kelly[pair_idx];
}

//+------------------------------------------------------------------+
//| Retrieve Confidence Score for External Access                    |
//| Returns: confidence metric in range [0, 1] for specified pair    |
//+------------------------------------------------------------------+
double Get_Confidence(int pair_idx)
{
   if(pair_idx < 0 || pair_idx >= ArraySize(Confidence_Score)) return 0.0;
   return Confidence_Score[pair_idx];
}