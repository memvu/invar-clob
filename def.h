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

using quantity_type = std::size_t;
using price_type = std::int32_t;
using id_type = std::uint64_t;
class Client;

enum class Side { Buy, Sell };

enum class OrderType { LimitOrder, MarketOrder, StopMarketOrder, StopLimitOrder };

enum class TimeInForcePolicy { GoodTillCancel, ImmediateOrCancel, FillOrKill };

enum class CommandType { AddOrder, CancelOrder, ReplaceOrder };

enum class PriorityAlg { PriceTime, Prorata };

// add more in future
enum class Instrument { AAPL, SPCX };

struct EventBatch {
   enum class CommandResult { Accepted, Rejected, None };
   // add more reason later
   enum class RejectReason { None, InvalidCLientId, InvalidQuantity, InvalidInstrument };

   CommandResult result{CommandResult::None};
   std::optional<id_type> orderId;

   struct Trade {
      id_type buySideId;
      id_type sellSideId;
      quantity_type quantity;
   };

   struct OrderCancelled {
      std::optional<RejectReason> rejectReason;
   };

   struct OrderReplaced {
      std::optional<RejectReason> rejectReason;
   };

   struct L2Delta {};
};

struct OrderRequest {
   Instrument stock;
   CommandType commandType;
   Side side;
   OrderType orderType;
   TimeInForcePolicy tifPolicy;
   quantity_type quantity{0};
   std::optional<price_type> price;
   std::optional<price_type> stopPrice;
};

struct OrderRecord : OrderRequest {
   id_type orderId;
   id_type clientId;
};

// modify order request is just an accepted order with a new quantity/price
struct ReplaceOrderRequest : OrderRecord {
   quantity_type new_quantity{0};
   price_type new_price{0};
};

class MatchingEngine {
private:
   // default
   const PriorityAlg priority_{PriorityAlg::PriceTime};

   class L3OrderBook {
      using PriceLevel = std::vector<id_type>; // for prorata

      std::map<price_type, PriceLevel, std::greater<price_type>> bids_;
      std::map<price_type, PriceLevel, std::less<price_type>> asks_;

      std::vector<id_type> resting_limits_;
   };

   std::vector<std::unique_ptr<Client>> clients_;
   std::unordered_map<id_type, Client *> idToClient_;
   std::unordered_map<id_type, OrderRecord> idToOrder_;

   std::unordered_map<Instrument, L3OrderBook> internal_l3s_;

   id_type curClientId_{1};

   id_type nextClientId() {
      return curClientId_++;
   };

public:
   explicit MatchingEngine(PriorityAlg alg)
       : priority_{alg} {};
   MatchingEngine(const MatchingEngine &rhs) = delete;
   MatchingEngine operator=(const MatchingEngine &rhs) = delete;

   // should move ctor be disabled?
   MatchingEngine(MatchingEngine &&rhs) = delete;
   MatchingEngine operator=(MatchingEngine &&rhs) = delete;

   const Client &createClient();
   decltype(auto) getOrderRecord(const Client &client, id_type orderId) const;

   EventBatch submitOrder(const Client &client, OrderRequest orderRequest);
   EventBatch cancelOrder(const Client &client, id_type orderId);
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
   EventBatch submitOrder(OrderRequest orderRequest);
   EventBatch cancelOrder(id_type orderId);
   EventBatch replaceOrder(ReplaceOrderRequest replaceOrderRequest);

   // explicitly prohibit copying/moving clients
   Client(const Client &rhs) = delete;
   Client operator=(const Client &rhs) = delete;
   Client(Client &&rhs) = delete;
   Client operator=(Client &&rhs) = delete;
};

#endif
