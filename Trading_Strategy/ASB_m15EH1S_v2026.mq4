//+------------------------------------------------------------------+
//|                                Ichimoku_Tenkan_Cloud_Basket.mq4  |
//|                        Professional Basket Trading System         |
//|                   Tenkan/Cloud Crossover with MTF Confirmation    |
//+------------------------------------------------------------------+
#property copyright "Eugene Lam"
#property link      ""
#property version   "2.00"
#property strict

#include <stdlib.mqh>
#include <stderror.mqh>

//+------------------------------------------------------------------+
//| Signal Direction Constants                                        |
//+------------------------------------------------------------------+
#define NONE 0
#define DOWN -1
#define UP   1

//+------------------------------------------------------------------+
//| Ichimoku Parameters                                               |
//+------------------------------------------------------------------+
extern string s1 = "========== ICHIMOKU SETTINGS ==========";
extern int tenkan = 9;      // Tenkan-sen Period
extern int kijun = 26;      // Kijun-sen Period
extern int senkou = 52;     // Senkou Span B Period

//+------------------------------------------------------------------+
//| Supporting Indicator Parameters                                   |
//+------------------------------------------------------------------+
extern string s2 = "========== MACD SETTINGS ==========";
extern int fastMA = 15;         // MACD Fast MA
extern int slowMA = 60;         // MACD Slow MA
extern int signal_period = 9;   // MACD Signal Period

//+------------------------------------------------------------------+
//| Trading Parameters                                                |
//+------------------------------------------------------------------+
extern string s3 = "========== TRADING PARAMETERS ==========";
extern double TradeSpacing_percentage_basket = 0.6;  // Trade Spacing (% of ATR)
extern double TakeProfit_percentage_basket = 0.3;    // Take Profit (% of ATR)
extern double switching_multiplier = 2.0;            // Position Switch Multiplier
extern double max_per_lot = 2.8;                    // Max Lot Per Order

//+------------------------------------------------------------------+
//| ATR Range Filter                                                  |
//+------------------------------------------------------------------+
extern string s4 = "========== ATR FILTER ==========";
extern double daily_ATR_pct_min = 0.2;  // Min Daily ATR% to Trade
extern double daily_ATR_pct_max = 0.9;  // Max Daily ATR% to Trade

//+------------------------------------------------------------------+
//| Progressive Lot Sizing                                            |
//+------------------------------------------------------------------+
extern string s5 = "========== LOT SIZES ==========";
extern double LS1  = 0.07;   // Lot Size 1
extern double LS2  = 0.14;   // Lot Size 2
extern double LS3  = 0.28;   // Lot Size 3
extern double LS4  = 0.56;   // Lot Size 4
extern double LS5  = 1.12;   // Lot Size 5
extern double LS6  = 2.24;   // Lot Size 6
extern double LS7  = 4.48;   // Lot Size 7
extern double LS8  = 8.96;   // Lot Size 8
extern double LS9  = 17.92;   // Lot Size 9
extern double LS10 = 20;   // Lot Size 10
extern double LS11 = 1.23;   // Lot Size 11
extern double LS12 = 1.24;   // Lot Size 12
extern double LS13 = 1.25;   // Lot Size 13
extern double LS14 = 1.26;   // Lot Size 14
extern double LS15 = 3.00;   // Lot Size 15
extern double LS16 = 3.10;   // Lot Size 16
extern double LS17 = 3.20;   // Lot Size 17
extern double LS18 = 3.30;   // Lot Size 18
extern double LS19 = 3.40;   // Lot Size 19
extern double LS20 = 3.50;   // Lot Size 20

//+------------------------------------------------------------------+
//| System Settings                                                   |
//+------------------------------------------------------------------+
extern string s6 = "========== SYSTEM ==========";
extern int MagicSeed = 1000;      // Magic Number Seed
extern bool ShowComment = false;  // Show Dashboard
extern int Slippage = 30;         // Slippage (points)

//+------------------------------------------------------------------+
//| Global Variables                                                  |
//+------------------------------------------------------------------+
int magic;
double pips;
double daily_ATR_pct;
double daily_ATR;
double TradeSpacing;
double TakeProfit;
double StopLoss = 0;

//+------------------------------------------------------------------+
//| Expert Initialization                                             |
//+------------------------------------------------------------------+
int OnInit()
{
   magic = MagicNumberGenerator();
   
   pips = Point;
   if(Digits == 3 || Digits == 5)
      pips *= 10;
   
   if(daily_ATR_pct_min >= daily_ATR_pct_max)
   {
      Alert("ERROR: daily_ATR_pct_min must be < daily_ATR_pct_max");
      return INIT_PARAMETERS_INCORRECT;
   }
   
   Print("===========================================");
   Print("Ichimoku Tenkan/Cloud Basket EA Initialized");
   Print("Symbol: ", Symbol());
   Print("Magic: ", magic);
   Print("===========================================");
   
   return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Expert Deinitialization                                           |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   Comment("");
   ObjectDelete("BuysBreakevenLine");
   ObjectDelete("SellsBreakevenLine");
}

//+------------------------------------------------------------------+
//| Expert Tick Function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
   static datetime candletime = 0;
   
   if(candletime != Time[0])
   {
      candletime = Time[0];
      
      daily_ATR = Get_ATR_values();
      daily_ATR_pct = Get_ATR_pct();
      
      if(daily_ATR <= 0)
      {
         Print("WARNING: Daily ATR is zero");
         return;
      }
      
      if(daily_ATR_pct < daily_ATR_pct_max && daily_ATR_pct > daily_ATR_pct_min)
      {
         CheckForSignal();
      }
   }
   
   UpdateBreakevenLines();
   
   if(ShowComment)
   {
      Comment(
         "\n========== ICHIMOKU BASKET EA ==========",
         "\nBUY Orders: ", TotalOpenBuyOrders(),
         "\nBUY Lots: ", DoubleToStr(TotalOpenBuyLots(), 2),
         "\nBUY Breakeven: ", DoubleToStr(BreakevenOfBuys(), Digits),
         "\n",
         "\nSELL Orders: ", TotalOpenSellOrders(),
         "\nSELL Lots: ", DoubleToStr(TotalOpenSellLots(), 2),
         "\nSELL Breakeven: ", DoubleToStr(BreakevenOfSells(), Digits),
         "\n",
         "\nDaily ATR: ", DoubleToStr(daily_ATR, 1), " pips",
         "\nATR%: ", DoubleToStr(daily_ATR_pct * 100, 1), "%",
         "\nBalance: $", DoubleToStr(AccountBalance(), 2)
      );
   }
}

//+------------------------------------------------------------------+
//| Main Signal Processing                                            |
//+------------------------------------------------------------------+
void CheckForSignal()
{
   int Kijun_m15 = Get_Kijun_Signals(NULL, PERIOD_M15, 1);
   
   if(Kijun_m15 == NONE)
      return;
   
   int Kijun_switch_H1_current = Get_Kijun_Signals(NULL, PERIOD_H1, 1);
   int Kijun_switch_H1_previous = Get_Kijun_Signals(NULL, PERIOD_H1, 2);
   
   //==================================================================
   // H1 KIJUN SWITCHING LOGIC
   //==================================================================
   if((Kijun_switch_H1_current == DOWN && Kijun_switch_H1_previous == UP) ||
      (Kijun_switch_H1_current == UP && Kijun_switch_H1_previous == DOWN))
   {
      if(TotalOpenBuyLots_with_no_sl() > 0 || TotalOpenSellLots_with_no_sl() > 0)
      {
         int Kijun_m5 = Get_Kijun_Signals(NULL, PERIOD_M5, 0);
         int Kijun_H1 = Get_Kijun_Signals(NULL, PERIOD_H1, 0);
         int Kijun_H4 = Get_Kijun_Signals(NULL, PERIOD_H4, 0);
         
         int MACD_m5 = Get_MACD_Signals(NULL, PERIOD_M5, 0);
         int MACD_m15 = Get_MACD_Signals(NULL, PERIOD_M15, 0);
         int MACD_H1 = Get_MACD_Signals(NULL, PERIOD_H1, 0);
         int MACD_H4 = Get_MACD_Signals(NULL, PERIOD_H4, 0);
         
         int AO_m5 = Get_AO_Signals(NULL, PERIOD_M5, 0);
         int AO_m15 = Get_AO_Signals(NULL, PERIOD_M15, 0);
         int AO_H1 = Get_AO_Signals(NULL, PERIOD_H1, 0);
         int AO_H4 = Get_AO_Signals(NULL, PERIOD_H4, 0);
         
         int AC_m5 = Get_AC_Signals(NULL, PERIOD_M5, 0);
         int AC_m15 = Get_AC_Signals(NULL, PERIOD_M15, 0);
         int AC_H1 = Get_AC_Signals(NULL, PERIOD_H1, 0);
         int AC_H4 = Get_AC_Signals(NULL, PERIOD_H4, 0);
         
         // Switch Long to Short
         if(MACD_m5 == DOWN && MACD_m15 == DOWN && MACD_H1 == DOWN && MACD_H4 == DOWN &&
            AO_m5 == DOWN && AO_m15 == DOWN && AO_H1 == DOWN && AO_H4 == DOWN &&
            AC_m5 == DOWN && AC_m15 == DOWN && AC_H1 == DOWN && AC_H4 == DOWN &&
            Kijun_m5 == DOWN && Kijun_m15 == DOWN && Kijun_H1 == DOWN && Kijun_H4 == DOWN)
         {
            if(Kijun_switch_H1_current == DOWN && Kijun_switch_H1_previous == UP)
            {
               if(TotalOpenBuyLots_with_no_sl() > 0 && TotalOpenSellLots_with_no_sl() == 0)
               {
                  SwitchLongToShort();
                  return;
               }
            }
         }
         
         // Switch Short to Long
         if(MACD_m5 == UP && MACD_m15 == UP && MACD_H1 == UP && MACD_H4 == UP &&
            AO_m5 == UP && AO_m15 == UP && AO_H1 == UP && AO_H4 == UP &&
            AC_m5 == UP && AC_m15 == UP && AC_H1 == UP && AC_H4 == UP &&
            Kijun_m5 == UP && Kijun_m15 == UP && Kijun_H1 == UP && Kijun_H4 == UP)
         {
            if(Kijun_switch_H1_current == UP && Kijun_switch_H1_previous == DOWN)
            {
               if(TotalOpenSellLots_with_no_sl() > 0 && TotalOpenBuyLots_with_no_sl() == 0)
               {
                  SwitchShortToLong();
                  return;
               }
            }
         }
      }
   }
   
   //==================================================================
   // TENKAN/CLOUD CROSSOVER ENTRY SIGNAL
   //==================================================================
   int TenkanCloudSignal = Get_TenkanCloud_CrossSignal(NULL, PERIOD_M15, 1);
   
   if(TenkanCloudSignal == NONE)
      return;
   
   if((Kijun_switch_H1_current == DOWN && Kijun_switch_H1_previous == UP) ||
      (Kijun_switch_H1_current == UP && Kijun_switch_H1_previous == DOWN))
      return;
   
   TradeSpacing = NormalizeDouble(daily_ATR, 2) * TradeSpacing_percentage_basket;
   
   int Kijun_m5 = Get_Kijun_Signals(NULL, PERIOD_M5, 0);
   int MACD_m5 = Get_MACD_Signals(NULL, PERIOD_M5, 0);
   int MACD_m15 = Get_MACD_Signals(NULL, PERIOD_M15, 0);
   int MACD_H1 = Get_MACD_Signals(NULL, PERIOD_H1, 0);
   int MACD_H4 = Get_MACD_Signals(NULL, PERIOD_H4, 0);
   
   int AO_m5 = Get_AO_Signals(NULL, PERIOD_M5, 0);
   int AO_m15 = Get_AO_Signals(NULL, PERIOD_M15, 0);
   int AO_H1 = Get_AO_Signals(NULL, PERIOD_H1, 0);
   int AO_H4 = Get_AO_Signals(NULL, PERIOD_H4, 0);
   
   int AC_m5 = Get_AC_Signals(NULL, PERIOD_M5, 0);
   int AC_m15 = Get_AC_Signals(NULL, PERIOD_M15, 0);
   int AC_H1 = Get_AC_Signals(NULL, PERIOD_H1, 0);
   int AC_H4 = Get_AC_Signals(NULL, PERIOD_H4, 0);
   
   //==================================================================
   // SELL ENTRY
   //==================================================================
   if(TenkanCloudSignal == DOWN)
   {
      bool full_bull = (MACD_m5 == UP && MACD_m15 == UP && MACD_H1 == UP && MACD_H4 == UP &&
                        AO_m5 == UP && AO_m15 == UP && AO_H1 == UP && AO_H4 == UP &&
                        AC_m5 == UP && AC_m15 == UP && AC_H1 == UP && AC_H4 == UP &&
                        Kijun_m5 == UP && Kijun_m15 == UP);
      
      if(!full_bull && Kijun_m15 == DOWN)
      {
         if(TotalOpenSellLots_with_sl() == 0)
         {
            if(TotalOpenSellOrders() > 0)
            {
               if(Bid - HighestSellPosition() >= TradeSpacing * pips)
                  EnterBasketTrade(OP_SELL);
            }
            else
            {
               EnterBasketTrade(OP_SELL);
            }
         }
         else
         {
            bool full_bear = (MACD_m5 == DOWN && MACD_m15 == DOWN && MACD_H1 == DOWN && MACD_H4 == DOWN &&
                             AO_m5 == DOWN && AO_m15 == DOWN && AO_H1 == DOWN && AO_H4 == DOWN &&
                             AC_m5 == DOWN && AC_m15 == DOWN && AC_H1 == DOWN && AC_H4 == DOWN &&
                             Kijun_m5 == DOWN && Kijun_m15 == DOWN);
            
            if(full_bear)
            {
               if(TotalOpenSellOrders() > 0)
               {
                  if(Bid - HighestSellPosition() >= TradeSpacing * pips)
                     EnterBasketTrade(OP_SELL);
               }
               else
               {
                  EnterBasketTrade(OP_SELL);
               }
            }
         }
      }
   }
   
   //==================================================================
   // BUY ENTRY
   //==================================================================
   if(TenkanCloudSignal == UP)
   {
      bool full_bear = (MACD_m5 == DOWN && MACD_m15 == DOWN && MACD_H1 == DOWN && MACD_H4 == DOWN &&
                        AO_m5 == DOWN && AO_m15 == DOWN && AO_H1 == DOWN && AO_H4 == DOWN &&
                        AC_m5 == DOWN && AC_m15 == DOWN && AC_H1 == DOWN && AC_H4 == DOWN &&
                        Kijun_m5 == DOWN && Kijun_m15 == DOWN);
      
      if(!full_bear && Kijun_m15 == UP)
      {
         if(TotalOpenBuyLots_with_sl() == 0)
         {
            if(TotalOpenBuyOrders() > 0)
            {
               if(LowestBuyPosition() - Ask >= TradeSpacing * pips)
                  EnterBasketTrade(OP_BUY);
            }
            else
            {
               EnterBasketTrade(OP_BUY);
            }
         }
         else
         {
            bool full_bull = (MACD_m5 == UP && MACD_m15 == UP && MACD_H1 == UP && MACD_H4 == UP &&
                             AO_m5 == UP && AO_m15 == UP && AO_H1 == UP && AO_H4 == UP &&
                             AC_m5 == UP && AC_m15 == UP && AC_H1 == UP && AC_H4 == UP &&
                             Kijun_m5 == UP && Kijun_m15 == UP);
            
            if(full_bull)
            {
               if(TotalOpenBuyOrders() > 0)
               {
                  if(LowestBuyPosition() - Ask >= TradeSpacing * pips)
                     EnterBasketTrade(OP_BUY);
               }
               else
               {
                  EnterBasketTrade(OP_BUY);
               }
            }
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Get Tenkan/Cloud Crossover Signal                                |
//+------------------------------------------------------------------+
int Get_TenkanCloud_CrossSignal(string symbol, int timeframe, int shift)
{
   double tenkan_now = iIchimoku(symbol, timeframe, tenkan, kijun, senkou, MODE_TENKANSEN, shift);
   double spanA_now = iIchimoku(symbol, timeframe, tenkan, kijun, senkou, MODE_SENKOUSPANA, shift);
   double spanB_now = iIchimoku(symbol, timeframe, tenkan, kijun, senkou, MODE_SENKOUSPANB, shift);
   
   double tenkan_prev = iIchimoku(symbol, timeframe, tenkan, kijun, senkou, MODE_TENKANSEN, shift + 1);
   double spanA_prev = iIchimoku(symbol, timeframe, tenkan, kijun, senkou, MODE_SENKOUSPANA, shift + 1);
   double spanB_prev = iIchimoku(symbol, timeframe, tenkan, kijun, senkou, MODE_SENKOUSPANB, shift + 1);
   
   double cloud_top_now = MathMax(spanA_now, spanB_now);
   double cloud_bottom_now = MathMin(spanA_now, spanB_now);
   double cloud_top_prev = MathMax(spanA_prev, spanB_prev);
   double cloud_bottom_prev = MathMin(spanA_prev, spanB_prev);
   
   if(tenkan_prev <= cloud_top_prev && tenkan_now > cloud_top_now)
      return UP;
   
   if(tenkan_prev >= cloud_bottom_prev && tenkan_now < cloud_bottom_now)
      return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Get Kijun Signal                                                  |
//+------------------------------------------------------------------+
int Get_Kijun_Signals(string tradepair, int timeframe, int candle)
{
   double close_price = iClose(tradepair, timeframe, candle);
   double Kijun_line = iIchimoku(tradepair, timeframe, tenkan, kijun, senkou, MODE_KIJUNSEN, candle);
   
   if(close_price > Kijun_line) return UP;
   if(close_price < Kijun_line) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Get MACD Signal                                                   |
//+------------------------------------------------------------------+
int Get_MACD_Signals(string tradepair, int timeframe, int candel)
{
   double current_MACD = iMACD(tradepair, timeframe, fastMA, slowMA, 9, 0, 0, candel);
   double previous_MACD = iMACD(tradepair, timeframe, fastMA, slowMA, 9, 0, 0, candel + 1);
   
   if(current_MACD > previous_MACD) return UP;
   if(current_MACD < previous_MACD) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Get AO Signal                                                     |
//+------------------------------------------------------------------+
int Get_AO_Signals(string tradepair, int timeframe, int candel)
{
   double current_AO = iAO(tradepair, timeframe, candel);
   double previous_AO = iAO(tradepair, timeframe, candel + 1);
   
   if(current_AO > previous_AO) return UP;
   if(current_AO < previous_AO) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Get AC Signal                                                     |
//+------------------------------------------------------------------+
int Get_AC_Signals(string tradepair, int timeframe, int candel)
{
   double current_AC = iAC(tradepair, timeframe, candel);
   double previous_AC = iAC(tradepair, timeframe, candel + 1);
   
   if(current_AC > previous_AC) return UP;
   if(current_AC < previous_AC) return DOWN;
   
   return NONE;
}

//+------------------------------------------------------------------+
//| Enter Basket Trade                                                |
//+------------------------------------------------------------------+
void EnterBasketTrade(int type)
{
   double price = (type == OP_BUY) ? Ask : Bid;
   double lotsize = CalculatedLotSize(type);
   
   if(type == OP_BUY && TotalOpenSellLots_with_sl() > 0)
   {
      lotsize = (TotalOpenBuyLots() + TotalOpenSellLots()) * switching_multiplier;
      Print("Adjusting BUY lotsize: ", lotsize);
   }
   else if(type == OP_SELL && TotalOpenBuyLots_with_sl() > 0)
   {
      lotsize = (TotalOpenBuyLots() + TotalOpenSellLots()) * switching_multiplier;
      Print("Adjusting SELL lotsize: ", lotsize);
   }
   
   TakeProfit = daily_ATR * TakeProfit_percentage_basket;
   
   double remaining = lotsize;
   int ticket = -1;
   
   while(remaining > max_per_lot)
   {
      ticket = OrderSend(Symbol(), type, NormalizeDouble(max_per_lot, 2), price, Slippage, 0, 0, 
                         "Basket", magic, 0, clrNONE);
      
      if(ticket > 0)
      {
         remaining -= max_per_lot;
      }
      else
      {
         Print("Multi-lot ERROR: ", GetLastError());
         break;
      }
   }
   
   if(remaining > 0)
   {
      ticket = OrderSend(Symbol(), type, NormalizeDouble(remaining, 2), price, Slippage, 0, 0, 
                         "Basket", magic, 0, clrNONE);
   }
   
   if(ticket > 0)
   {
      Sleep(500);
      
      if(!OrderSelect(ticket, SELECT_BY_TICKET))
      {
         Print("Failed to select order ", ticket);
         return;
      }
      
      double tp = 0;
      
      if(type == OP_SELL)
         tp = NormalizeDouble(BreakevenOfSells() - (TakeProfit * pips), Digits);
      else
         tp = NormalizeDouble(BreakevenOfBuys() + (TakeProfit * pips), Digits);
      
      double min_stop = MarketInfo(Symbol(), MODE_STOPLEVEL) * Point;
      
      if(MathAbs(price - tp) < min_stop)
      {
         if(type == OP_SELL)
            tp = NormalizeDouble(price - (min_stop + 5 * Point), Digits);
         else
            tp = NormalizeDouble(price + (min_stop + 5 * Point), Digits);
      }
      
      for(int i = 0; i < OrdersTotal(); i++)
      {
         if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         {
            if(OrderMagicNumber() == magic && OrderType() == type)
            {
               bool mod_result = OrderModify(OrderTicket(), OrderOpenPrice(), OrderStopLoss(), tp, 0, clrNONE);
               if(!mod_result)
               {
                  Print("Modify ERROR ", OrderTicket(), ": ", GetLastError());
               }
            }
         }
      }
      
      if(type == OP_BUY && TotalOpenSellLots_with_sl() > 0)
      {
         for(int i = 0; i < OrdersTotal(); i++)
         {
            if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
            {
               if(OrderMagicNumber() == magic && OrderType() == OP_SELL && OrderStopLoss() != 0)
               {
                  bool mod_result = OrderModify(OrderTicket(), OrderOpenPrice(), tp, OrderTakeProfit(), 0, clrNONE);
                  if(!mod_result)
                  {
                     Print("Switch SELL SL update ERROR: ", GetLastError());
                  }
               }
            }
         }
      }
      else if(type == OP_SELL && TotalOpenBuyLots_with_sl() > 0)
      {
         for(int i = 0; i < OrdersTotal(); i++)
         {
            if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
            {
               if(OrderMagicNumber() == magic && OrderType() == OP_BUY && OrderStopLoss() != 0)
               {
                  bool mod_result = OrderModify(OrderTicket(), OrderOpenPrice(), tp, OrderTakeProfit(), 0, clrNONE);
                  if(!mod_result)
                  {
                     Print("Switch BUY SL update ERROR: ", GetLastError());
                  }
               }
            }
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Switch Long to Short                                              |
//+------------------------------------------------------------------+
void SwitchLongToShort()
{
   Print("========== SWITCHING LONG TO SHORT ==========");
   
   double price = Bid;
   double lotsize = NormalizeDouble(TotalOpenBuyLots() * switching_multiplier, 2);
   double breakeven = NormalizeDouble(BreakevenOfBuys(), Digits);
   double min_stop = MarketInfo(Symbol(), MODE_STOPLEVEL) * Point;
   
   TakeProfit = daily_ATR * TakeProfit_percentage_basket;
   
   double long_sl = price - (breakeven - price) - (TakeProfit * pips);
   
   if(MathAbs(price - long_sl) < min_stop)
   {
      long_sl = NormalizeDouble(price - (min_stop + 5 * Point), Digits);
   }
   
   int ticket = OrderSend(Symbol(), OP_SELL, lotsize, price, Slippage, 0, long_sl, 
                          "Switch_Short", magic, 0, clrRed);
   
   if(ticket > 0)
   {
      Sleep(500);
      
      for(int i = 0; i < OrdersTotal(); i++)
      {
         if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         {
            if(OrderMagicNumber() == magic && OrderType() == OP_BUY)
            {
               bool mod_result = OrderModify(OrderTicket(), OrderOpenPrice(), long_sl, 0, 0, clrBlue);
               if(!mod_result)
               {
                  Print("Switch LONG ERROR: ", GetLastError());
               }
            }
         }
      }
      
      Print("Switched LONG→SHORT: ", lotsize);
   }
}

//+------------------------------------------------------------------+
//| Switch Short to Long                                              |
//+------------------------------------------------------------------+
void SwitchShortToLong()
{
   Print("========== SWITCHING SHORT TO LONG ==========");
   
   double price = Ask;
   double lotsize = NormalizeDouble(TotalOpenSellLots() * switching_multiplier, 2);
   double breakeven = NormalizeDouble(BreakevenOfSells(), Digits);
   double min_stop = MarketInfo(Symbol(), MODE_STOPLEVEL) * Point;
   
   TakeProfit = daily_ATR * TakeProfit_percentage_basket;
   
   double short_sl = price + (price - breakeven) + (TakeProfit * pips);
   
   if(MathAbs(price - short_sl) < min_stop)
   {
      short_sl = NormalizeDouble(price + (min_stop + 5 * Point), Digits);
   }
   
   int ticket = OrderSend(Symbol(), OP_BUY, lotsize, price, Slippage, 0, short_sl, 
                          "Switch_Long", magic, 0, clrBlue);
   
   if(ticket > 0)
   {
      Sleep(500);
      
      for(int i = 0; i < OrdersTotal(); i++)
      {
         if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         {
            if(OrderMagicNumber() == magic && OrderType() == OP_SELL)
            {
               bool mod_result = OrderModify(OrderTicket(), OrderOpenPrice(), short_sl, 0, 0, clrRed);
               if(!mod_result)
               {
                  Print("Switch SHORT ERROR: ", GetLastError());
               }
            }
         }
      }
      
      Print("Switched SHORT→LONG: ", lotsize);
   }
}

//+------------------------------------------------------------------+
//| HELPER FUNCTIONS                                                  |
//+------------------------------------------------------------------+

double CalculatedLotSize(int type)
{
   int tradeNumber = (type == OP_SELL) ? TotalOpenSellOrders() + 1 : TotalOpenBuyOrders() + 1;
   
   switch(tradeNumber)
   {
      case 1:  return LS1;
      case 2:  return LS2;
      case 3:  return LS3;
      case 4:  return LS4;
      case 5:  return LS5;
      case 6:  return LS6;
      case 7:  return LS7;
      case 8:  return LS8;
      case 9:  return LS9;
      case 10: return LS10;
      case 11: return LS11;
      case 12: return LS12;
      case 13: return LS13;
      case 14: return LS14;
      case 15: return LS15;
      case 16: return LS16;
      case 17: return LS17;
      case 18: return LS18;
      case 19: return LS19;
      case 20: return LS20;
      default: return LS1;
   }
}

double Get_ATR_values()
{
   double ATR = iATR(NULL, PERIOD_D1, 14, 0);
   
   if(pips > 0)
      return ATR / pips;
   
   return 0;
}

double Get_ATR_pct()
{
   double ATR = iATR(NULL, PERIOD_D1, 14, 0);
   
   if(ATR <= 0 || pips <= 0)
      return 0;
   
   double atr_pips = ATR / pips;
   double day_high = iHigh(NULL, PERIOD_D1, 0);
   double day_low = iLow(NULL, PERIOD_D1, 0);
   double range = (day_high - day_low) / pips;
   
   return range / atr_pips;
}

int MagicNumberGenerator()
{
   string mySymbol = StringSubstr(Symbol(), 0, 6);
   int pairNumber = 0;
   
   if(mySymbol == "AUDCAD") pairNumber = 1;
   else if(mySymbol == "AUDCHF") pairNumber = 2;
   else if(mySymbol == "AUDJPY") pairNumber = 3;
   else if(mySymbol == "AUDNZD") pairNumber = 4;
   else if(mySymbol == "AUDUSD") pairNumber = 5;
   else if(mySymbol == "EURUSD") pairNumber = 15;
   else if(mySymbol == "GBPUSD") pairNumber = 21;
   else if(mySymbol == "USDJPY") pairNumber = 28;
   else if(mySymbol == "XAUUSD") pairNumber = 30;
   else pairNumber = 99;
   
   return MagicSeed + (pairNumber * 1000) + Period();
}

void UpdateBreakevenLines()
{
   if(TotalOpenBuyOrders() > 0)
   {
      if(ObjectFind("BuysBreakevenLine") != 0)
         ObjectCreate("BuysBreakevenLine", OBJ_HLINE, 0, 0, BreakevenOfBuys());
      else
         ObjectMove("BuysBreakevenLine", 0, 0, BreakevenOfBuys());
      
      ObjectSet("BuysBreakevenLine", OBJPROP_COLOR, clrBlue);
   }
   else
      ObjectDelete("BuysBreakevenLine");
   
   if(TotalOpenSellOrders() > 0)
   {
      if(ObjectFind("SellsBreakevenLine") != 0)
         ObjectCreate("SellsBreakevenLine", OBJ_HLINE, 0, 0, BreakevenOfSells());
      else
         ObjectMove("SellsBreakevenLine", 0, 0, BreakevenOfSells());
      
      ObjectSet("SellsBreakevenLine", OBJPROP_COLOR, clrRed);
   }
   else
      ObjectDelete("SellsBreakevenLine");
}

int TotalOpenBuyOrders()
{
   int total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_BUY)
            total++;
   return total;
}

int TotalOpenSellOrders()
{
   int total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_SELL)
            total++;
   return total;
}

double TotalOpenBuyLots()
{
   double total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_BUY)
            total += OrderLots();
   return total;
}

double TotalOpenSellLots()
{
   double total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_SELL)
            total += OrderLots();
   return total;
}

double TotalOpenBuyLots_with_no_sl()
{
   double total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_BUY && OrderStopLoss() == 0)
            total += OrderLots();
   return total;
}

double TotalOpenSellLots_with_no_sl()
{
   double total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_SELL && OrderStopLoss() == 0)
            total += OrderLots();
   return total;
}

double TotalOpenBuyLots_with_sl()
{
   double total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_BUY && OrderStopLoss() != 0)
            total += OrderLots();
   return total;
}

double TotalOpenSellLots_with_sl()
{
   double total = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
         if(OrderMagicNumber() == magic && OrderType() == OP_SELL && OrderStopLoss() != 0)
            total += OrderLots();
   return total;
}

double LowestBuyPosition()
{
   double lowest = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
   {
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
      {
         if(OrderMagicNumber() == magic && OrderType() == OP_BUY)
         {
            if(lowest == 0 || OrderOpenPrice() < lowest)
               lowest = OrderOpenPrice();
         }
      }
   }
   return lowest;
}

double HighestSellPosition()
{
   double highest = 0;
   for(int i = OrdersTotal() - 1; i >= 0; i--)
   {
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
      {
         if(OrderMagicNumber() == magic && OrderType() == OP_SELL)
         {
            if(OrderOpenPrice() > highest)
               highest = OrderOpenPrice();
         }
      }
   }
   return highest;
}

double BreakevenOfBuys()
{
   double total_lots = 0;
   double weighted_sum = 0;
   
   for(int i = 0; i < OrdersTotal(); i++)
   {
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
      {
         if(OrderMagicNumber() == magic && OrderType() == OP_BUY)
         {
            weighted_sum += OrderOpenPrice() * OrderLots();
            total_lots += OrderLots();
         }
      }
   }
   
   if(total_lots > 0)
      return weighted_sum / total_lots;
   
   return 0;
}

double BreakevenOfSells()
{
   double total_lots = 0;
   double weighted_sum = 0;
   
   for(int i = 0; i < OrdersTotal(); i++)
   {
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
      {
         if(OrderMagicNumber() == magic && OrderType() == OP_SELL)
         {
            weighted_sum += OrderOpenPrice() * OrderLots();
            total_lots += OrderLots();
         }
      }
   }
   
   if(total_lots > 0)
      return weighted_sum / total_lots;
   
   return 0;
}
//+------------------------------------------------------------------+