#include "def.h"

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

EventBatch Client::submitOrder(SubmitOrderRequest order_request) {
   return owner_->submitOrder(*this, order_request);
}

EventBatch Client::cancelOrder(CancelOrderRequest cancelOrderRequest) {
   return owner_->cancelOrder(*this, cancelOrderRequest);
}

EventBatch Client::replaceOrder(ReplaceOrderRequest replaceOrderRequest) {
   return owner_->replaceOrder(*this, replaceOrderRequest);
}

EventBatch MatchingEngine::submitOrder(const Client &client, SubmitOrderRequest orderRequest) {
}

EventBatch MatchingEngine::cancelOrder(const Client &client, CancelOrderRequest cancelOrderRequest) {
}

EventBatch MatchingEngine::replaceOrder(const Client &client, ReplaceOrderRequest replaceOrderRequest) {
}
