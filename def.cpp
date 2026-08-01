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
}

EventBatch
MatchingEngine::execute(const Client &client, const CancelOrderRequest &cancelOrderRequest, id_type seq) {
}

EventBatch
MatchingEngine::execute(const Client &client, const ReplaceOrderRequest &replaceOrderRequest, id_type seq) {
}

EventBatch MatchingEngine::process(const Client &client, const Request &rq) {
   const id_type seq = nextSequence();
   return std::visit([&](const auto &arg) { return execute(client, arg, seq); }, rq);
}
