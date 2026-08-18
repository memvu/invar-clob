#include "def.h"
#include <algorithm>
#include <stdexcept>
#include <variant>

using namespace clob;

const Client &MatchingEngine::createClient()
{
   Client *new_client = new Client(this, nextClientId());
   clients_.emplace_back(std::unique_ptr<Client>(new_client));
   idToClient_.try_emplace(new_client->getClientId(), new_client);
   return *clients_.back();
}

id_type Client::getClientId() const
{
   return clientId_;
}

decltype(auto) MatchingEngine::getOrderRecord(const Client &client, id_type orderId) const
{
   auto record = idToOrder_.find(orderId);
   if (record == idToOrder_.end() || record->second.clientId != client.getClientId())
      return static_cast<const OrderRecord *>(nullptr);
   return &record->second;
}

decltype(auto) Client::getOrderRecord(id_type orderId) const
{
   return owner_->getOrderRecord(*this, orderId);
}

EventBatch Client::submitOrder(const SubmitOrderRequest &order_request)
{
   return owner_->process(*this, order_request);
}

EventBatch Client::cancelOrder(const CancelOrderRequest &cancelOrderRequest)
{
   return owner_->process(*this, cancelOrderRequest);
}

EventBatch Client::replaceOrder(const ReplaceOrderRequest &replaceOrderRequest)
{
   return owner_->process(*this, replaceOrderRequest);
}

EventBatch
MatchingEngine::execute(const Client &client, const SubmitOrderRequest &submitOrderRequest, id_type seq)
{
   EventBatch ret;
   ret.sequenceNo = seq;
}

EventBatch
MatchingEngine::execute(const Client &client, const CancelOrderRequest &cancelOrderRequest, id_type seq)
{
   EventBatch ret;
   auto id{cancelOrderRequest.orderId};
   ret.sequenceNo = seq;
   if (!idToClient_.contains(client.getClientId())) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidCLientId;
   }
   else if (!idToOrder_.contains(id) || idToOrder_[id].clientId != client.getClientId()) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidOrderId;
   }
   else {
      auto &order = idToOrder_[id];
      const auto &price = order.price;

      order.state = OrderRecord::State::Cancelled;

      internal_l3_.removeStop(id);
      internal_l3_.removeAsk(price, id);
      internal_l3_.removeBid(price, id);
      ret.result = EventBatch::CommandResult::Accepted;
      ret.events.push_back(
          EventBatch::OrderCancelled(id, EventBatch::OrderCancelled::CancelReason::UserRequest));
   }

   return ret;
}

EventBatch
MatchingEngine::execute(const Client &client, const ReplaceOrderRequest &replaceOrderRequest, id_type seq)
{
   EventBatch ret;
   auto id{replaceOrderRequest.orderId};
   ret.sequenceNo = seq;

   if (!idToClient_.contains(client.getClientId())) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidCLientId;
   }
   else if (!idToOrder_.contains(id) || idToOrder_[id].clientId != client.getClientId()) {
      ret.result = EventBatch::CommandResult::Rejected;
      ret.rejectReason = EventBatch::RejectReason::InvalidOrderId;
   }
   else {
      auto &order = idToOrder_[id];
      const auto oldPrice = order.price;
      const auto &newPrice = replaceOrderRequest.newPrice;
      const auto &newQuantity = replaceOrderRequest.newQuantity;

      if (replaceOrderRequest.newQuantity <= order.executedQuantity) {
         ret.result = EventBatch::CommandResult::Rejected;
         ret.rejectReason = EventBatch::RejectReason::InvalidQuantity;
      }
      else if (order.state == OrderRecord::State::Cancelled) {
         ret.result = EventBatch::CommandResult::Rejected;
         ret.rejectReason = EventBatch::RejectReason::OrderAlreadyCancelled;
      }
      else if (order.state == OrderRecord::State::Filled) {
         ret.result = EventBatch::CommandResult::Rejected;
         ret.rejectReason = EventBatch::RejectReason::OrderAlreadyFilled;
      }
      else if (order.state == OrderRecord::State::DormantStop) {
         ret.result = EventBatch::CommandResult::Rejected;
         ret.rejectReason = EventBatch::RejectReason::OrderIsDormantStop;
      }
      else if (!internal_l3_.priceExists(oldPrice)) {
         throw std::runtime_error("ERROR: OrderID exists but no price match");
      }
      else {
         const auto oldQuantity = order.quantity;
         order.quantity = newQuantity;
         order.remainingQuantity = newQuantity - order.executedQuantity;
         order.price = newPrice;

         if (oldPrice != newPrice || oldQuantity < newQuantity) {
            if (order.side == Side::Buy) {
               internal_l3_.removeBid(oldPrice, id);
            }
            else {
               internal_l3_.removeAsk(oldPrice, id);
            }
         }

         ret.events.push_back(EventBatch::OrderReplaced{id, newQuantity, newPrice});

         match(ret, id);

         if (order.side == Side::Buy) {
            internal_l3_.addBid(newPrice, id);
         }
         else {
            internal_l3_.addAsk(newPrice, id);
         }
      }
   }

   return ret;
}

EventBatch MatchingEngine::process(const Client &client, const Request &rq)
{
   const id_type seq = nextSequence();
   return std::visit([&](const auto &arg) { return execute(client, arg, seq); }, rq);
}

const price_type MatchingEngine::L3OrderBook::getBestBid() const
{
   if (bids_.empty())
      return 0;
   return bids_.begin()->first;
}

const price_type MatchingEngine::L3OrderBook::getBestAsk() const
{
   if (asks_.empty())
      return 0;
   return asks_.begin()->first;
}

bool MatchingEngine::L3OrderBook::priceExists(price_type price) const
{
   return (bids_.contains(price)) || (asks_.contains(price));
}

MatchingEngine::L3OrderBook::PriceLevel &MatchingEngine::L3OrderBook::getLevel(price_type price)
{
   if (!priceExists(price))
      throw std::logic_error("Price doesnt exist");
   return (bids_.contains(price)) ? bids_[price] : asks_[price];
}

void MatchingEngine::L3OrderBook::addBid(price_type price, id_type id)
{
   if (bids_.contains(price)) {
      auto &lv = bids_[price];
      auto it = std::find(lv.cbegin(), lv.cend(), id);
      if (it == lv.cend())
         lv.push_back(id);
   }
   else {
      bids_.insert({price, PriceLevel{id}});
   }
}

void MatchingEngine::L3OrderBook::addAsk(price_type price, id_type id)
{
   if (asks_.contains(price)) {
      auto &lv = asks_[price];
      auto it = std::find(lv.cbegin(), lv.cend(), id);
      if (it == lv.cend())
         lv.push_back(id);
   }
   else {
      asks_.insert({price, PriceLevel{id}});
   }
}

void MatchingEngine::L3OrderBook::addStop(id_type id)
{
   dormant_stops_.insert(id);
}

void MatchingEngine::L3OrderBook::removeBid(price_type price, id_type id)
{
   if (bids_.contains(price)) {
      auto &lv = bids_[price];
      std::erase(lv, id);
   }
}

void MatchingEngine::L3OrderBook::removeAsk(price_type price, id_type id)
{
   if (asks_.contains(price)) {
      auto &lv = asks_[price];
      std::erase(lv, id);
   }
}

void MatchingEngine::L3OrderBook::removeStop(id_type id)
{
   dormant_stops_.erase(id);
}
