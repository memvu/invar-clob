#include "def.h"
#include <variant>

using namespace clob;

const Client &MatchingEngine::createClient() {
   Client *new_client = new Client(this, nextClientId());
   clients_.emplace_back(std::unique_ptr<Client>(new_client));
   idToClient_.try_emplace(new_client->getClientId(), new_client);
   return *clients_.back();
}

id_type Client::getClientId() const {
   return clientId_;
}

decltype(auto) MatchingEngine::getOrderRecord(const Client &client, id_type orderId) const {
   auto record = idToOrder_.find(orderId);
   if (record == idToOrder_.end() || record->second.clientId != client.getClientId())
      return static_cast<const OrderRecord *>(nullptr);
   return &record->second;
}

decltype(auto) Client::getOrderRecord(id_type orderId) const {
   return owner_->getOrderRecord(*this, orderId);
}

EventBatch Client::submitOrder(const SubmitOrderRequest &order_request) {
   return owner_->process(*this, order_request);
}

EventBatch Client::cancelOrder(const CancelOrderRequest &cancelOrderRequest) {
   return owner_->process(*this, cancelOrderRequest);
}

EventBatch Client::replaceOrder(const ReplaceOrderRequest &replaceOrderRequest) {
   return owner_->process(*this, replaceOrderRequest);
}

EventBatch
MatchingEngine::execute(const Client &client, const SubmitOrderRequest &submitOrderRequest, id_type seq) {
   EventBatch ret;
   ret.sequenceNo = seq;
}

EventBatch
MatchingEngine::execute(const Client &client, const CancelOrderRequest &cancelOrderRequest, id_type seq) {
   EventBatch ret;
   auto id{cancelOrderRequest.orderId};
   ret.sequenceNo = seq;
   if (!idToClient_.contains(client.getClientId())) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidCLientId;
   } else if (!idToOrder_.contains(id)) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidOrderId;
   } else {
      const auto &order = idToOrder_[id];

      if (internal_l3_.dormant_stops_.contains(id))
         internal_l3_.dormant_stops_.erase(id);
      // if not dormant then is resting
      else {
         if (order.price < internal_l3_.asks_.begin()->first) {
            internal_l3_.asks_.erase(id);
         } else {
            internal_l3_.bids_.erase(id);
         }
      }

      idToOrder_.erase(id);
   }
   ret.result = EventBatch::CommandResult::Accepted;
   ret.events.push_back(
       EventBatch::OrderCancelled(id, EventBatch::OrderCancelled::CancelReason::UserRequest));

   return ret;
}

EventBatch
MatchingEngine::execute(const Client &client, const ReplaceOrderRequest &replaceOrderRequest, id_type seq) {
   EventBatch ret;
   auto id{replaceOrderRequest.orderId};
   ret.sequenceNo = seq;

   if (!idToClient_.contains(client.getClientId())) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidCLientId;
   } else if (!idToOrder_.contains(id)) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidOrderId;
   } else {
      auto &order = idToOrder_[id];
      auto &price = order.price;

      if (internal_l3_.dormant_stops_.contains(id)) {
         if (replaceOrderRequest.new_price != 0)
            order.price = replaceOrderRequest.new_price;
         if (replaceOrderRequest.new_quantity != 0)
            order.quantity = replaceOrderRequest.new_quantity;
      } else {
         auto &level = (price < internal_l3_.asks_.begin()->first) ? internal_l3_.bids_[price]
                                                                   : internal_l3_.asks_[price];
         if (config_.alg == EngineConfig::PriorityAlg::PriceTime) {
            // std::swap()
         }
      }
   }

   // process_order();

   return ret;
}

EventBatch MatchingEngine::process(const Client &client, const Request &rq) {
   const id_type seq = nextSequence();
   return std::visit([&](const auto &arg) { return execute(client, arg, seq); }, rq);
}

const price_type MatchingEngine::L3OrderBook::getBestBid() const {
   if (bids_.empty())
      return 0;
   return bids_.begin()->first;
}

const price_type MatchingEngine::L3OrderBook::getBestAsk() const {
   if (asks_.empty())
      return 0;
   return asks_.begin()->first;
}

bool MatchingEngine::L3OrderBook::priceExists(price_type price) const {
   return (bids_.contains(price)) || (asks_.contains(price));
}

MatchingEngine::L3OrderBook::PriceLevel &MatchingEngine::L3OrderBook::getLevel(price_type price) {
   if (price < getBestAsk()) {
      return bids_[price];
   }
   return asks_[price];
}
