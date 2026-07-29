#ifndef INTERNAL_DEF_H
#define INTERNAL_DEF_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include <memory>
#include <map>
#include <queue>
#include <unordered_set>

namespace clob {

using quantity_type = std::size_t;
using price_type = std::int64_t;
using id_type = std::uint64_t;
class Client;

enum class Instrument { AAPL, SPCX };

struct EngineConfig {
   enum class PriorityAlg { PriceTime, Prorata };

   PriorityAlg alg;
   Instrument instrument;
};

enum class Side { Buy, Sell };

enum class OrderType { LimitOrder, MarketOrder, StopMarketOrder, StopLimitOrder };

enum class TimeInForcePolicy { GoodTillCancel, ImmediateOrCancel, FillOrKill };

struct EventBatch {
   enum class CommandResult { Accepted, Rejected, None };
   // add more reason later
   enum class RejectReason { None, InvalidCLientId, InvalidQuantity, InvalidInstrument };

   struct Trade {
      id_type buySideId;
      id_type sellSideId;
      price_type price;
      quantity_type quantity;
   };

   struct OrderCancelled {
      enum class CancelReason { UserRequest, UnfilledMarketRemainder, SelfTradeRemainder };
      id_type orderId;
      CancelReason reason;
   };

   struct OrderReplaced {
      id_type orderId;
      quantity_type newQuantity;
      price_type newPrice;
   };

   struct L2Delta {
      std::vector<Trade> trades;
   };

   CommandResult result{CommandResult::None};
   std::optional<RejectReason> rejectReason;
};

struct SubmitOrderRequest {
   Instrument instrument; // must match instrument of engine
   Side side;
   OrderType orderType;
   TimeInForcePolicy tifPolicy;
   quantity_type quantity{0};
   std::optional<price_type> price;
   std::optional<price_type> stopPrice;
};

struct ReplaceOrderRequest {
   id_type orderId;
   quantity_type new_quantity{0};
   price_type new_price{0};
};

struct CancelOrderRequest {
   id_type orderId;
};

struct OrderRecord : SubmitOrderRequest {
   id_type orderId;
   id_type clientId;
};

class MatchingEngine {
private:
   // default
   const EngineConfig config_;

   class L3OrderBook {
      using PriceLevel = std::vector<id_type>;

      std::map<price_type, PriceLevel, std::greater<price_type>> bids_;
      std::map<price_type, PriceLevel, std::less<price_type>> asks_;

      std::unordered_set<id_type> inert_stop_orders_;
   };

   std::vector<std::unique_ptr<Client>> clients_;
   std::unordered_map<id_type, Client *> idToClient_;
   std::unordered_map<id_type, OrderRecord> idToOrder_;
   price_type lastTradedPrice{};

   L3OrderBook internal_l3_;

   id_type curClientId_{1};

   id_type nextClientId() {
      return curClientId_++;
   };

public:
   explicit MatchingEngine(const EngineConfig &cfg)
       : config_{cfg} {};
   MatchingEngine(const MatchingEngine &rhs) = delete;
   MatchingEngine operator=(const MatchingEngine &rhs) = delete;
   MatchingEngine(MatchingEngine &&rhs) = delete;
   MatchingEngine operator=(MatchingEngine &&rhs) = delete;

   const Client &createClient();
   decltype(auto) getOrderRecord(const Client &client, id_type orderId) const;

   EventBatch submitOrder(const Client &client, SubmitOrderRequest submitOrderRequest);
   EventBatch cancelOrder(const Client &client, CancelOrderRequest cancelOrderRequest);
   EventBatch replaceOrder(const Client &client, ReplaceOrderRequest replaceOrderRequest);
};

class Client {
private:
   friend class MatchingEngine;
   MatchingEngine *owner_{};
   id_type clientId_{};

   explicit Client(MatchingEngine *owner, const id_type clientId)
       : owner_(owner)
       , clientId_(clientId) {};

public:
   id_type getClientId() const;
   decltype(auto) getOrderRecord(id_type orderId) const;
   EventBatch submitOrder(SubmitOrderRequest orderRequest);
   EventBatch cancelOrder(CancelOrderRequest cancelOrderRequest);
   EventBatch replaceOrder(ReplaceOrderRequest replaceOrderRequest);

   // explicitly prohibit copying/moving clients
   Client(const Client &rhs) = delete;
   Client operator=(const Client &rhs) = delete;
   Client(Client &&rhs) = delete;
   Client operator=(Client &&rhs) = delete;
};
}; // namespace clob

#endif
