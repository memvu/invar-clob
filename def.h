#ifndef INTERNAL_DEF_H
#define INTERNAL_DEF_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
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
   // TODO: add more reason
   enum class RejectReason {
      None,
      InvalidCLientId,
      InvalidQuantity,
      InvalidInstrument,
      InvalidOrderId,
      OrderAlreadyCancelled,
      OrderAlreadyFilled,
      OrderIsDormantStop
   };

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

      OrderCancelled(id_type id, CancelReason rs)
          : orderId{id}
          , reason{rs}
      {
      }
   };

   struct OrderReplaced {
      id_type orderId;
      quantity_type newQuantity;
      price_type newPrice;
   };

   using Event = std::variant<Trade, OrderReplaced, OrderCancelled>;

   id_type sequenceNo;
   CommandResult result{CommandResult::None};
   std::optional<RejectReason> rejectReason;
   std::vector<Event> events;
};

struct SubmitOrderRequest {
   Instrument instrument; // must match instrument of engine
   Side side;
   OrderType orderType;
   TimeInForcePolicy tifPolicy;
   quantity_type quantity{0};
   price_type price;
   std::optional<price_type> stopPrice;
};

struct ReplaceOrderRequest {
   id_type orderId;
   quantity_type newQuantity{0};
   price_type newPrice{0};
};

struct CancelOrderRequest {
   id_type orderId;
};

struct OrderRecord : SubmitOrderRequest {
   enum class State { Filled, Active, DormantStop, Cancelled };
   id_type orderId;
   id_type clientId;
   quantity_type remainingQuantity{quantity};
   quantity_type executedQuantity{0};
   State state;
};

class MatchingEngine {
private:
   // default
   const EngineConfig config_;

   struct L3OrderBook {
      using PriceLevel = std::vector<id_type>;

      std::map<price_type, PriceLevel, std::greater<price_type>> bids_;
      std::map<price_type, PriceLevel, std::less<price_type>> asks_;

      std::unordered_set<id_type> dormant_stops_;

      void addBid(price_type price, id_type id);
      void addAsk(price_type price, id_type id);
      void addStop(id_type id);

      void removeBid(price_type price, id_type id);
      void removeAsk(price_type price, id_type id);
      void removeStop(id_type id);

      const price_type getBestBid() const;
      const price_type getBestAsk() const;

      bool priceExists(price_type price) const;

      // precondition: price exists in orderbook
      PriceLevel &getLevel(price_type price);
   };

   std::vector<std::unique_ptr<Client>> clients_;
   std::unordered_map<id_type, Client *> idToClient_;
   std::unordered_map<id_type, OrderRecord> idToOrder_;
   price_type lastTradedPrice{};

   L3OrderBook internal_l3_;

   id_type curClientId_{1};

   id_type nextClientId()
   {
      return curClientId_++;
   };

   id_type eventSequence{1};

   id_type nextSequence()
   {
      return eventSequence++;
   };

   EventBatch execute(const Client &client, const SubmitOrderRequest &submitOrderRequest, id_type seq);
   EventBatch execute(const Client &client, const CancelOrderRequest &cancelOrderRequest, id_type seq);
   EventBatch execute(const Client &client, const ReplaceOrderRequest &replaceOrderRequest, id_type seq);

   void match(EventBatch &batch, id_type orderId);

public:
   explicit MatchingEngine(const EngineConfig &cfg)
       : config_{cfg} {};
   MatchingEngine(const MatchingEngine &rhs) = delete;
   MatchingEngine operator=(const MatchingEngine &rhs) = delete;
   MatchingEngine(MatchingEngine &&rhs) = delete;
   MatchingEngine operator=(MatchingEngine &&rhs) = delete;

   const Client &createClient();
   decltype(auto) getOrderRecord(const Client &client, id_type orderId) const;

   using Request = std::variant<SubmitOrderRequest, CancelOrderRequest, ReplaceOrderRequest>;
   EventBatch process(const Client &client, const Request &rq);
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
   EventBatch submitOrder(const SubmitOrderRequest &orderRequest);
   EventBatch cancelOrder(const CancelOrderRequest &cancelOrderRequest);
   EventBatch replaceOrder(const ReplaceOrderRequest &replaceOrderRequest);

   // explicitly prohibit copying/moving clients
   Client(const Client &rhs) = delete;
   Client operator=(const Client &rhs) = delete;
   Client(Client &&rhs) = delete;
   Client operator=(Client &&rhs) = delete;
};
}; // namespace clob

#endif
