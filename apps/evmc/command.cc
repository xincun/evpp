#include "command.h"

#include <memcached/protocol_binary.h>

#include "memcache_client.h"
#include "vbucket_config.h"

namespace evmc {

void Command::Launch(MemcacheClientPtr memc_client) {
    // id_ = memc_client->next_id();
    if (!memc_client->conn()) {
        // TODO : 触发网络错误回调
        LOG_ERROR << "Command bad memc_client " << memc_client << " id=" << id_;
        return;
    }

    BufferPtr buf = RequestBuffer();
    memc_client->conn()->Send(buf->data(), buf->size());
}

uint16_t Command::server_id() const {
    if (server_id_history_.empty()) {
        return BAD_SERVER_ID;
    } 
    return server_id_history_.back(); 
}

bool Command::ShouldRetry() const {
    LOG_INFO << "ShouldRetry vbucket=" << vbucket_id()
             << " server_id=" << server_id()
             << " len=" << server_id_history_.size();
    return false;
    return server_id_history_.size() < 2;
}

BufferPtr SetCommand::RequestBuffer() const {
    protocol_binary_request_header req;
    bzero((void*)&req, sizeof(req));

    req.request.magic  = PROTOCOL_BINARY_REQ;
    req.request.opcode = PROTOCOL_BINARY_CMD_SET;
    req.request.keylen = htons(key_.size());
    req.request.extlen = 8;
    req.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.request.vbucket  = vbucket_id();
    req.request.opaque   = id();
    size_t bodylen = req.request.extlen + key_.size() + value_.size();
    req.request.bodylen = htonl(static_cast<uint32_t>(bodylen));

    BufferPtr buf(new evpp::Buffer(sizeof(protocol_binary_request_header) + key_.size()));
    buf->Append((void*)&req, sizeof(req));
    buf->AppendInt32(flags_);
    buf->AppendInt32(expire_);
    buf->Append(key_.data(), key_.size());
    buf->Append(value_.data(), value_.size());

    return buf;
}

BufferPtr GetCommand::RequestBuffer() const {
    protocol_binary_request_header req;
    bzero((void*)&req, sizeof(req));

    req.request.magic  = PROTOCOL_BINARY_REQ;
    req.request.opcode = PROTOCOL_BINARY_CMD_GET;
    req.request.keylen = htons(key_.size());
    req.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.request.vbucket  = vbucket_id();
    req.request.opaque   = id();
    req.request.bodylen = htonl(key_.size());

    BufferPtr buf(new evpp::Buffer(sizeof(protocol_binary_request_header) + key_.size()));
    buf->Append((void*)&req, sizeof(req));
    buf->Append(key_.data(), key_.size());

    return buf;
}

void MultiGetCommand::OnMultiGetCommandDone(int resp_code, const std::string& key, const std::string& value) {
    if (resp_code == PROTOCOL_BINARY_RESPONSE_SUCCESS) {
        mget_result_.get_result_map_[key] = GetResult(resp_code, value);
    }
    mget_result_.code = resp_code;

    if (caller_loop()) {
        caller_loop()->RunInLoop(std::bind(mget_callback_, mget_result_));
    } else {
        mget_callback_(mget_result_);
    }
}

void MultiGetCommand::OnMultiGetCommandOneResponse(int resp_code, const std::string& key, const std::string& value) {
    LOG_INFO << "OnMultiGetCommandOneResponse " << key << " " << resp_code << " " << value;
    if (resp_code == PROTOCOL_BINARY_RESPONSE_SUCCESS) {
        mget_result_.get_result_map_[key] = GetResult(resp_code, value);
    }
    mget_result_.code = resp_code;
}

BufferPtr MultiGetCommand::RequestBuffer() const {
    BufferPtr buf(new evpp::Buffer(50 * keys_.size()));  // 预分配长度多数情况够长

    for(size_t i = 0; i < keys_.size(); ++i) {
        protocol_binary_request_header req;
        bzero((void*)&req, sizeof(req));
        req.request.magic    = PROTOCOL_BINARY_REQ;
        if (i < keys_.size() - 1) {
            req.request.opcode   = PROTOCOL_BINARY_CMD_GETKQ;
        } else {
            req.request.opcode   = PROTOCOL_BINARY_CMD_GETK;
        }
        req.request.keylen   = htons(keys_[i].size());
        req.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
        req.request.vbucket  = vbucket_id();
        req.request.opaque   = id();
        req.request.bodylen  = htonl(keys_[i].size());

        buf->Append((void*)&req, sizeof(req));
        buf->Append(keys_[i].data(), keys_[i].size());
    }

    return buf;
}


BufferPtr RemoveCommand::RequestBuffer() const {
    protocol_binary_request_header req;
    bzero((void*)&req, sizeof(req));

    req.request.magic  = PROTOCOL_BINARY_REQ;
    req.request.opcode = PROTOCOL_BINARY_CMD_DELETE;
    req.request.keylen = htons(key_.size());
    req.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.request.vbucket  = vbucket_id();
    req.request.opaque   = id();
    req.request.bodylen = htonl(static_cast<uint32_t>(key_.size()));

    BufferPtr buf(new evpp::Buffer(sizeof(protocol_binary_request_header) + key_.size()));
    buf->Append((void*)&req, sizeof(req));
    buf->Append(key_.data(), key_.size());

    return buf;
}

}

