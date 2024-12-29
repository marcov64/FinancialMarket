//#define EIGENLIB			// uncomment to use Eigen linear algebra library

#include "fun_head_fast.h"
#include "s-adapt.cpp"

// do not add Equations in this area

object *group, *market;

MODELBEGIN


EQUATION("STest")
/*
Experimental tracking variable adapting to the market price following a logistic-like pattern
The parameter 's' controls the speed of adaptation, the higher the quicker
*/

v[1] = V("s");
v[0]=sadapt(p, "Price", "STest", v[1]);

RESULT(v[0])

EQUATION("refPrice")
/*
Private exptected price of the asset for each agent
The reference price is used to determine whether to post a bid to buy/sell the asset
*/
v[0] = V("sAgent");
//v[1]=sadapt(p, "Price", "refPrice", v[0]);
v[1] = VL("Price", 1);
v[2] = VL("refPrice", 1);
v[3]=v[2]*v[0]+(1-v[0])*v[1];
RESULT(v[3] )

EQUATION("Action")
/*
Action of an agent.
When the past action is -1 or +1 there is a bid currently posted. The bid is left for MaxTime periods, after which is removed.
When past action is zero it means that there are no bids currently posted and the agent considers whether to post a bid.

The present implementation posts a bid if the reference price is sufficiently far away from the past period market price.
The bid needs to fix three info:
- direction (buy or sell)
- bid price
- number of units of the asset

The bidding routine consider bidding 20% of the units in case of selling and 20% of the cash in case of buying. Constraints apply, so that a bidder cannot sell or buy unless it has sufficient assets or cash, respectively.

The price is set at the half between the reference price and past market price. 

*/

v[0] = VL("Action", 1);
if(v[0]!=0)
{//there is an active bid pending on the market
 v[1] = V("TimeProposal");
 v[2] = V("MaxTime");
 if((double)t - v[1] <v[2]) //if the time is not expired
   END_EQUATION(v[0]);

//withdraw the bid   
 v[3] = V("Id");
 cur=p->hook;
 if(v[0]==-1)
  { 
   v[7]=INCRS(market, "NumSell", -1);
   if(v[7]==0)
    {
     WRITES(cur, "SId", 0);
     WRITES(cur, "SPrice", -1);
     cur->hook=NULL;
    }
   else 
    DELETE(cur); 
  }  
 else
  {
   v[8]=INCRS(market, "NumBuy", -1);
   if(v[8]==0)
    {
     WRITES(cur, "BId", 0);
     WRITES(cur, "BPrice", -1);
     cur->hook=NULL;
    }
   else 
    DELETE(cur); 
  } 
p->hook=NULL;
} 

v[3] = V("refPrice");

//Previous period's price.
v[2]=VLS(market,"Price",0);
v[30] = V("DevDistPrice");
if(abs(v[3]-v[2])<v[30])
 END_EQUATION(0);

v[31] = V("Asset");
v[32] = V("Cash");
 
if(v[3]<v[2])
  {if(v[31]==0)
     END_EQUATION(0);
   v[33]=max(1, round(v[31]*0.2) );
   v[10]=-1; //sell if the ref price is lower than current price
   WRITE("bidQuantity", v[33]);
  }
else
 {
  v[34]=floor(v[32]*0.2/v[3]);
  if(v[34]==0)
   END_EQUATION(0);
  WRITE("bidQuantity", v[34]); 
  v[10]=1; //buy if ref price is higher than current price
 }
v[4]=(v[3]+v[2])/2;//bid price is halfway between ref and current


v[20] = V("NumAgents");
v[21] = V("ThresholdBusyMarket");
if(v[10]==-1) //if it is a sale
 {
  v[22] = V("NumSell");
  v[23]=v[22]/v[20];
  if(v[23]>v[21])
   {
    INCRS(market, "RejectedSell", 1);
    END_EQUATION(0); //Sorry, busy market
   } 
  INCRS(market, "NumSell", 1);
  cur = SEARCHS(market, "Sell");
  if(cur->hook!=NULL)
    cur = ADDOBJS(market, "Sell");
  WRITES(cur, "SId", V("Id"));
  WRITES(cur, "SPrice", v[4]);
  cur->hook=p;
  p->hook=cur;
 }
else
 {
  v[24] = V("NumBuy");
  v[23]=v[24]/v[20];
  if(v[23]>v[21])
   {
    INCRS(market, "RejectedBuy", 1);
    END_EQUATION(0); //Sorry, busy market
   }
  INCRS(market, "NumBuy", 1);
  cur = SEARCHS(market, "Buy");
  if(cur->hook!=NULL)
   cur=ADDOBJS(market,"Buy");
  WRITES(cur, "BId", V("Id"));
  WRITES(cur, "BPrice", v[4]);
  cur->hook=p;
  p->hook=cur;
 }


//Store this time step, for future decisions on whether to withdraw the proposal
WRITE("TimeProposal", (double)t);

RESULT(v[10] )

EQUATION("AgentsAction")
/*
Global variable setting ensuring that all agents update their Action
Before letting agents make their decision the routine computes the average reference price and the standard deviation from the actual past market price.
*/

v[0]=v[1]=v[2]=v[10]=v[11]=0;
v[7] = VL("Price", 1);
CYCLE(cur, "Agent")
 {
  v[3] = VS(cur, "refPrice")-v[7];
  v[0]+=v[3];
  v[1]+=v[3]*v[3];
  v[2]++;
  v[10] += VS(cur, "Asset");
  v[11] += VS(cur, "Cash");
 } 
WRITE("TotAsset", v[10]);
WRITE("TotCash", v[11]);
v[4]=v[0]/v[2];
v[5]=v[1]/v[2];
v[6]=v[5]-v[4]*v[4];
v[7]=sqrt(v[6]);
WRITE("AvDistPrice", v[4]);
WRITE("DevDistPrice", v[7]);

WRITES(market, "RejectedSell", 0);
WRITES(market, "RejectedBuy", 0);
CYCLE(cur, "Agent")
{
 VS(cur, "Action");
}
 
RESULT(1 )


EQUATION("MarketAction")
/*
MarketAction
Computed after each and every agent has stored its proposal, this function
completes as many transactions as possible, coupling the best offers (lowest prices)
with the best requests (highest prices). This is the mechanism used in Italian
exchange.
In the model's structure, Market contains two sets of entities for the selling
and buying proposals. The function sorts them placing in the initial places the
most attractive ones, and proceeds matching the best elements until the best
Buy (highest proposed price) is higher than the best Sell (lowest proposed price).
This function returns the number of transactions that actually took place. It also
computes the average price of the transactions, stored in Price. 
*/

V("AgentsAction");


SORT("Buy", "BPrice", "DOWN");
SORT("Sell", "SPrice", "UP");
cur = SEARCH("Buy");
cur1 = SEARCH("Sell");

v[2]=VS(cur,"BPrice");
v[3]=VS(cur1,"SPrice");

for(v[9]=v[6]=v[5]=v[4]=0, v[0]=0; cur1!=NULL && cur!=NULL && cur1->hook!=NULL && cur->hook!=NULL && v[2]>v[3] ; )
 {
  //Prices of the current best proposals
  if(v[9]++>100000)
   INTERACT("LOOP", v[9]);
  
 
//INTERACT("CHECK", v[2]);
  //Pointers to the subsequent entities, stored to continue the cycle
  cur2=brother(cur);
  cur3=brother(cur1);
  
  if(v[2]>v[3]) //If the Buy price is higher than the Sell price...
   {             //the transaction takes place, notifying the fact to the two involved agents
    cur4=cur->hook; //the agent who posted the buy
    cur5=cur1->hook; //the agent who posted the sell
    v[30]=min(VS(cur4, "bidQuantity"),VS(cur5, "bidQuantity"));
    v[31]=(v[2]+v[3])/2;
    INCRS(cur4, "Asset", v[30]);
    INCRS(cur5, "Asset", -v[30]);
    INCRS(cur4, "Cash", -v[30]*v[31]);
    INCRS(cur5, "Cash", v[30]*v[31]);
    
    v[32] = INCRS(cur4, "bidQuantity",-v[30]); 
    v[33] = INCRS(cur5, "bidQuantity",-v[30]);
     
    WRITES(cur5,"Action",0);
    if(v[32]==0)//buyer fulfilled its bid
     {
      cur4->hook=NULL;
      v[22]=INCRS(market, "NumBuy", -1);
      WRITES(cur4,"Action",0);
      if(v[22]>0)
        DELETE(cur);
      else
       {cur->hook=NULL;
        WRITES(cur, "BPrice", -1);  
        WRITES(cur, "BId", 0);
       }   
      cur=cur2;//move to the next buyer bid  
     }
    if(v[33]==0) //seller fulfilled its bid
     {
      cur5->hook=NULL;
      v[21]=INCRS(market, "NumSell", -1);
      WRITES(cur5,"Action",0);
      if(v[21]>0)
        DELETE(cur1);
      else
       {cur1->hook=NULL;
        WRITES(cur1, "SPrice", -1);  
        WRITES(cur1, "SId", 0);
       }   
      cur1=cur3;
     }
    v[5]+=(v[2]+v[3])/2; //Storing variable for the computation of price
    v[4]++;              //storing variable for teh computation of number of transaction
    v[6]+=v[30];
    if(cur!=NULL && cur1!=NULL)
     {
      v[2]=VS(cur,"BPrice");
      v[3]=VS(cur1,"SPrice");
     } 
   }
 }


 if(v[4]!=0) //if at least one transaction took place
  WRITE("Price",v[5]/v[4]); //compute the average price of the time step
WRITE("Volume", v[6]);
//return the number of transactions, useless

RESULT(v[4] )


EQUATION("QuickPrice")
/*
Compute a "quick" smoothed indicator of Price with the function
QuickPrice(t) = QuickPrice(t-1) * (1-CoeffQPrice) + Price(t) * CoeffQPrice

The higher QuickPrice, the more rapid is the response of the indicator to
the changes in Price. Note that an identical indicator is computed in SlowPrice,
the only difference being the value of the coefficients:
CoeffQPrice > CoeffSPrice
*/
v[0] = V("Price");
v[1] = VL("QuickPrice", 1);
v[2] = V("CoeffQPrice");

v[3]=v[1]*(1-v[2])+v[0]*v[2];

RESULT(v[3] )

EQUATION("SlowPrice")
/*
Compute a "slow" smoothed indicator of Price with the function
SlowPrice(t) = SlowPrice(t-1) * (1-CoeffSPrice) + Price(t) * CoeffSPrice

The lower CoeffSPrice, the slower is the response of the indicator to
the changes in Price. Note that an identical indicator is computed in QuickPrice,
the only difference being the value of the coefficients:
CoeffQPrice > CoeffSPrice
*/
v[0] = V("Price");
v[1] = VL("SlowPrice", 1);
v[2] = V("CoeffSPrice");

v[3]=v[1]*(1-v[2])+v[0]*v[2];

RESULT(v[3] )



EQUATION("Init")
/*
Initialization routine performing actions required before starting the simulation and never repeated again:
- create a group of object Agent
- assign to each a random initial reference price, cash and number of assets
- compute the initial (fake) past market price

*/
market=SEARCH("Market");
group=SEARCH("Group");
v[0] = V("NumAgents");
ADDNOBJS(group, "Agent", v[0]-1);
v[1]=1;

v[2] = V("minAsset");
v[3] = V("maxAsset");
v[4] = V("minPrice");
v[5] = V("maxPrice");
v[8] = V("minCash");
v[9] = V("maxCash");
v[11] = V("minS");
v[12] = V("maxS");

v[6]=0;
CYCLES(group, cur, "Agent")
{
 WRITELS(cur, "Action", 0, 0);
 WRITES(cur, "Id", v[1]++);
 v[7]=round(uniform(v[2], v[3]));
 WRITES(cur, "Asset", v[7]);
 v[7]=uniform(v[4], v[5]);
 v[6]+=v[7];
 WRITELS(cur, "refPrice", v[7], 0);
 WRITELS(cur, "refPrice", v[7], -1);
 v[10]=round(uniform(v[8], v[9]));
 WRITES(cur, "Cash", v[10]);
 WRITELS(cur, "Action", 0, t-1);
 v[13]=uniform(v[11], v[12]);
 WRITES(cur, "sAgent", v[13]);
 
}
v[14]=v[6]/v[0];
WRITELS(market, "Price", v[14],0);

PARAMETER;

RESULT(1 )


MODELEND

// do not add Equations in this area

void close_sim( void )
{
	// close simulation special commands go here
}









