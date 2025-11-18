

#ifndef DOCA_FUCK_H_
#define DOCA_FUCK_H_

#include <doca_error.h>
#include <doca_types.h>
#include <rdma/rdma_cma.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct doca_buf;
struct doca_dev;
struct doca_devinfo;
struct doca_sync_event_remote_net;

enum doca_rdma_transport_type {
    DOCA_RDMA_TRANSPORT_TYPE_RC,
    DOCA_RDMA_TRANSPORT_TYPE_DC,
};

struct doca_rdma_gid {
    uint8_t raw[DOCA_GID_BYTE_LENGTH];
};

enum doca_rdma_addr_type {
    DOCA_RDMA_ADDR_TYPE_IPv4,
    DOCA_RDMA_ADDR_TYPE_IPv6,
    DOCA_RDMA_ADDR_TYPE_GID,
};

struct doca_rdma;

struct doca_rdma_addr;

struct doca_rdma_connection;

typedef uint64_t doca_dpa_dev_rdma_t;

struct doca_gpu_dev_rdma;

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_create(struct doca_dev * dev, struct doca_rdma ** rdma);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_destroy(struct doca_rdma * rdma);

DOCA_EXPERIMENTAL
struct doca_ctx * doca_rdma_as_ctx(struct doca_rdma * rdma);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_export(struct doca_rdma * rdma, const void ** local_rdma_conn_details,
                              size_t * local_rdma_conn_details_size, struct doca_rdma_connection ** rdma_connection);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connect(struct doca_rdma * rdma, const void * remote_rdma_conn_details,
                               size_t remote_rdma_conn_details_size, struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_addr_create(enum doca_rdma_addr_type addr_type, const char * address, uint16_t port,
                                   struct doca_rdma_addr ** addr);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_addr_destroy(struct doca_rdma_addr * addr);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_start_listen_to_port(struct doca_rdma * rdma, uint16_t port);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_stop_listen_to_port(struct doca_rdma * rdma, uint16_t port);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connection_accept(struct doca_rdma_connection * rdma_connection, void * private_data,
                                         uint8_t private_data_len);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connection_reject(struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connect_to_addr(struct doca_rdma * rdma, struct doca_rdma_addr * addr,
                                       union doca_data connection_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connection_disconnect(struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_bridge_prepare_connection(struct doca_rdma * rdma, struct rdma_cm_id * cm_id,
                                                 struct doca_rdma_connection ** rdma_connection);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_bridge_accept(struct doca_rdma * rdma, void * private_data, uint8_t private_data_len,
                                     struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_bridge_established(struct doca_rdma * rdma, struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_get_max_recv_queue_size(const struct doca_devinfo * devinfo, uint32_t * max_recv_queue_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_get_max_send_queue_size(const struct doca_devinfo * devinfo, uint32_t * max_send_queue_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_get_max_send_buf_list_len(const struct doca_devinfo * devinfo,
                                                     uint32_t * max_send_buf_list_len);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_get_max_message_size(const struct doca_devinfo * devinfo, uint32_t * max_message_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_get_gid_table_size(const struct doca_devinfo * devinfo, uint32_t * gid_table_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_get_gid(const struct doca_devinfo * devinfo, uint32_t start_index, uint32_t num_entries,
                                   struct doca_rdma_gid * gid_array);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_transport_type_is_supported(const struct doca_devinfo * devinfo,
                                                       enum doca_rdma_transport_type transport_type);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_send_queue_size(struct doca_rdma * rdma, uint32_t send_queue_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_recv_queue_size(struct doca_rdma * rdma, uint32_t recv_queue_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_max_send_buf_list_len(struct doca_rdma * rdma, uint32_t max_send_buf_list_len);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_transport_type(struct doca_rdma * rdma, enum doca_rdma_transport_type transport_type);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_mtu(struct doca_rdma * rdma, enum doca_mtu_size mtu);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_permissions(struct doca_rdma * rdma, uint32_t permissions);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_grh_enabled(struct doca_rdma * rdma, uint8_t grh_enabled);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_gid_index(struct doca_rdma * rdma, uint32_t gid_index);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_sl(struct doca_rdma * rdma, uint32_t sl);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_connection_request_timeout(struct doca_rdma * rdma, uint16_t timeout);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connection_set_user_data(struct doca_rdma_connection * rdma_connection,
                                                union doca_data connection_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_max_num_connections(struct doca_rdma * rdma, uint16_t max_num_connections);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_rnr_retry_count(struct doca_rdma * rdma, uint8_t rnr_retry_count);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_send_queue_size(const struct doca_rdma * rdma, uint32_t * send_queue_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_recv_queue_size(const struct doca_rdma * rdma, uint32_t * recv_queue_size);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_max_send_buf_list_len(const struct doca_rdma * rdma, uint32_t * max_send_buf_list_len);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_transport_type(const struct doca_rdma * rdma,
                                          enum doca_rdma_transport_type * transport_type);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_mtu(const struct doca_rdma * rdma, enum doca_mtu_size * mtu);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_permissions(struct doca_rdma * rdma, uint32_t * permissions);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_grh_enabled(const struct doca_rdma * rdma, uint8_t * grh_enabled);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_gid_index(const struct doca_rdma * rdma, uint32_t * gid_index);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_sl(const struct doca_rdma * rdma, uint32_t * sl);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_dpa_handle(struct doca_rdma * rdma, doca_dpa_dev_rdma_t * dpa_rdma);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_gpu_handle(struct doca_rdma * rdma, struct doca_gpu_dev_rdma ** gpu_rdma);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_connection_request_timeout(const struct doca_rdma * rdma, uint16_t * timeout);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connection_get_addr(const struct doca_rdma_connection * rdma_connection,
                                           struct doca_rdma_addr ** addr);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_addr_get_params(struct doca_rdma_addr * addr, enum doca_rdma_addr_type * addr_type,
                                       const char ** address, uint16_t * port);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connection_get_user_data(const struct doca_rdma_connection * rdma_connection,
                                                union doca_data * connection_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_connection_get_id(const struct doca_rdma_connection * rdma_connection, uint32_t * connection_id);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_max_num_connections(struct doca_rdma * rdma, uint16_t * max_num_connections);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_get_rnr_retry_count(const struct doca_rdma * rdma, uint8_t * rnr_retry_count);

typedef void (*doca_rdma_connection_request_cb_t)(struct doca_rdma_connection * rdma_connection,
                                                  union doca_data ctx_user_data);

typedef void (*doca_rdma_connection_established_cb_t)(struct doca_rdma_connection * rdma_connection,
                                                      union doca_data connection_user_data,
                                                      union doca_data ctx_user_data);

typedef void (*doca_rdma_connection_failure_cb_t)(struct doca_rdma_connection * rdma_connection,
                                                  union doca_data connection_user_data, union doca_data ctx_user_data);

typedef void (*doca_rdma_connection_disconnection_cb_t)(struct doca_rdma_connection * rdma_connection,
                                                        union doca_data connection_user_data,
                                                        union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_set_connection_state_callbacks(
    struct doca_rdma * rdma, doca_rdma_connection_request_cb_t doca_rdma_connect_request_cb,
    doca_rdma_connection_established_cb_t doca_rdma_connect_established_cb,
    doca_rdma_connection_failure_cb_t doca_rdma_connect_failure_cb,
    doca_rdma_connection_disconnection_cb_t doca_rdma_disconnect_cb);

struct doca_rdma_task_receive;

enum doca_rdma_opcode {
    DOCA_RDMA_OPCODE_RECV_SEND = 0,
    DOCA_RDMA_OPCODE_RECV_SEND_WITH_IMM,
    DOCA_RDMA_OPCODE_RECV_WRITE_WITH_IMM,
};

typedef void (*doca_rdma_task_receive_completion_cb_t)(struct doca_rdma_task_receive * task,
                                                       union doca_data task_user_data, union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_receive_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_receive_get_max_dst_buf_list_len(const struct doca_devinfo * devinfo,
                                                                 enum doca_rdma_transport_type transport_type,
                                                                 uint32_t * max_buf_list_len);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_receive_set_conf(struct doca_rdma * rdma,
                                             doca_rdma_task_receive_completion_cb_t successful_task_completion_cb,
                                             doca_rdma_task_receive_completion_cb_t error_task_completion_cb,
                                             uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_receive_set_dst_buf_list_len(struct doca_rdma * rdma, uint32_t buf_list_len);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_receive_get_dst_buf_list_len(const struct doca_rdma * rdma, uint32_t * buf_list_len);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_receive_allocate_init(struct doca_rdma * rdma, struct doca_buf * dst_buf,
                                                  union doca_data user_data, struct doca_rdma_task_receive ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_receive_as_task(struct doca_rdma_task_receive * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_receive_set_dst_buf(struct doca_rdma_task_receive * task, struct doca_buf * dst_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_receive_get_dst_buf(const struct doca_rdma_task_receive * task);

DOCA_EXPERIMENTAL
enum doca_rdma_opcode doca_rdma_task_receive_get_result_opcode(const struct doca_rdma_task_receive * task);

DOCA_EXPERIMENTAL
uint32_t doca_rdma_task_receive_get_result_len(const struct doca_rdma_task_receive * task);

DOCA_EXPERIMENTAL
doca_be32_t doca_rdma_task_receive_get_result_immediate_data(const struct doca_rdma_task_receive * task);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_receive_get_result_rdma_connection(
    const struct doca_rdma_task_receive * task);

struct doca_rdma_task_send;

typedef void (*doca_rdma_task_send_completion_cb_t)(struct doca_rdma_task_send * task, union doca_data task_user_data,
                                                    union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_send_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_send_set_conf(struct doca_rdma * rdma,
                                          doca_rdma_task_send_completion_cb_t successful_task_completion_cb,
                                          doca_rdma_task_send_completion_cb_t error_task_completion_cb,
                                          uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_send_allocate_init(struct doca_rdma * rdma, struct doca_rdma_connection * rdma_connection,
                                               const struct doca_buf * src_buf, union doca_data user_data,
                                               struct doca_rdma_task_send ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_send_as_task(struct doca_rdma_task_send * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_send_set_src_buf(struct doca_rdma_task_send * task, const struct doca_buf * src_buf);

DOCA_EXPERIMENTAL
const struct doca_buf * doca_rdma_task_send_get_src_buf(const struct doca_rdma_task_send * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_send_set_rdma_connection(struct doca_rdma_task_send * task,
                                             struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_send_get_rdma_connection(const struct doca_rdma_task_send * task);

struct doca_rdma_task_send_imm;

typedef void (*doca_rdma_task_send_imm_completion_cb_t)(struct doca_rdma_task_send_imm * task,
                                                        union doca_data task_user_data, union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_send_imm_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_send_imm_set_conf(struct doca_rdma * rdma,
                                              doca_rdma_task_send_imm_completion_cb_t successful_task_completion_cb,
                                              doca_rdma_task_send_imm_completion_cb_t error_task_completion_cb,
                                              uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_send_imm_allocate_init(struct doca_rdma * rdma,
                                                   struct doca_rdma_connection * rdma_connection,
                                                   const struct doca_buf * src_buf, doca_be32_t immediate_data,
                                                   union doca_data user_data, struct doca_rdma_task_send_imm ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_send_imm_as_task(struct doca_rdma_task_send_imm * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_send_imm_set_src_buf(struct doca_rdma_task_send_imm * task, const struct doca_buf * src_buf);

DOCA_EXPERIMENTAL
const struct doca_buf * doca_rdma_task_send_imm_get_src_buf(const struct doca_rdma_task_send_imm * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_send_imm_set_immediate_data(struct doca_rdma_task_send_imm * task, doca_be32_t immediate_data);

DOCA_EXPERIMENTAL
doca_be32_t doca_rdma_task_send_imm_get_immediate_data(const struct doca_rdma_task_send_imm * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_send_imm_set_rdma_connection(struct doca_rdma_task_send_imm * task,
                                                 struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_send_imm_get_rdma_connection(
    const struct doca_rdma_task_send_imm * task);

struct doca_rdma_task_read;

typedef void (*doca_rdma_task_read_completion_cb_t)(struct doca_rdma_task_read * task, union doca_data task_user_data,
                                                    union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_read_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_read_set_conf(struct doca_rdma * rdma,
                                          doca_rdma_task_read_completion_cb_t successful_task_completion_cb,
                                          doca_rdma_task_read_completion_cb_t error_task_completion_cb,
                                          uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_read_allocate_init(struct doca_rdma * rdma, struct doca_rdma_connection * rdma_connection,
                                               const struct doca_buf * src_buf, struct doca_buf * dst_buf,
                                               union doca_data user_data, struct doca_rdma_task_read ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_read_as_task(struct doca_rdma_task_read * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_read_set_src_buf(struct doca_rdma_task_read * task, const struct doca_buf * src_buf);

DOCA_EXPERIMENTAL
const struct doca_buf * doca_rdma_task_read_get_src_buf(const struct doca_rdma_task_read * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_read_set_dst_buf(struct doca_rdma_task_read * task, struct doca_buf * dst_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_read_get_dst_buf(const struct doca_rdma_task_read * task);

DOCA_EXPERIMENTAL
uint32_t doca_rdma_task_read_get_result_len(const struct doca_rdma_task_read * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_read_set_rdma_connection(struct doca_rdma_task_read * task,
                                             struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_read_get_rdma_connection(const struct doca_rdma_task_read * task);

struct doca_rdma_task_write;

typedef void (*doca_rdma_task_write_completion_cb_t)(struct doca_rdma_task_write * task, union doca_data task_user_data,
                                                     union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_write_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_write_set_conf(struct doca_rdma * rdma,
                                           doca_rdma_task_write_completion_cb_t successful_task_completion_cb,
                                           doca_rdma_task_write_completion_cb_t error_task_completion_cb,
                                           uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_write_allocate_init(struct doca_rdma * rdma, struct doca_rdma_connection * rdma_connection,
                                                const struct doca_buf * src_buf, struct doca_buf * dst_buf,
                                                union doca_data user_data, struct doca_rdma_task_write ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_write_as_task(struct doca_rdma_task_write * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_write_set_src_buf(struct doca_rdma_task_write * task, const struct doca_buf * src_buf);

DOCA_EXPERIMENTAL
const struct doca_buf * doca_rdma_task_write_get_src_buf(const struct doca_rdma_task_write * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_write_set_dst_buf(struct doca_rdma_task_write * task, struct doca_buf * dst_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_write_get_dst_buf(const struct doca_rdma_task_write * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_write_set_rdma_connection(struct doca_rdma_task_write * task,
                                              struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_write_get_rdma_connection(const struct doca_rdma_task_write * task);

struct doca_rdma_task_write_imm;

typedef void (*doca_rdma_task_write_imm_completion_cb_t)(struct doca_rdma_task_write_imm * task,
                                                         union doca_data task_user_data, union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_write_imm_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_write_imm_set_conf(struct doca_rdma * rdma,
                                               doca_rdma_task_write_imm_completion_cb_t successful_task_completion_cb,
                                               doca_rdma_task_write_imm_completion_cb_t error_task_completion_cb,
                                               uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_write_imm_allocate_init(struct doca_rdma * rdma,
                                                    struct doca_rdma_connection * rdma_connection,
                                                    const struct doca_buf * src_buf, struct doca_buf * dst_buf,
                                                    doca_be32_t immediate_data, union doca_data user_data,
                                                    struct doca_rdma_task_write_imm ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_write_imm_as_task(struct doca_rdma_task_write_imm * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_write_imm_set_src_buf(struct doca_rdma_task_write_imm * task, const struct doca_buf * src_buf);

DOCA_EXPERIMENTAL
const struct doca_buf * doca_rdma_task_write_imm_get_src_buf(const struct doca_rdma_task_write_imm * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_write_imm_set_dst_buf(struct doca_rdma_task_write_imm * task, struct doca_buf * dst_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_write_imm_get_dst_buf(const struct doca_rdma_task_write_imm * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_write_imm_set_immediate_data(struct doca_rdma_task_write_imm * task, doca_be32_t immediate_data);

DOCA_EXPERIMENTAL
doca_be32_t doca_rdma_task_write_imm_get_immediate_data(const struct doca_rdma_task_write_imm * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_write_imm_set_rdma_connection(struct doca_rdma_task_write_imm * task,
                                                  struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_write_imm_get_rdma_connection(
    const struct doca_rdma_task_write_imm * task);

struct doca_rdma_task_atomic_cmp_swp;

typedef void (*doca_rdma_task_atomic_cmp_swp_completion_cb_t)(struct doca_rdma_task_atomic_cmp_swp * task,
                                                              union doca_data task_user_data,
                                                              union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_atomic_cmp_swp_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_atomic_cmp_swp_set_conf(
    struct doca_rdma * rdma, doca_rdma_task_atomic_cmp_swp_completion_cb_t successful_task_completion_cb,
    doca_rdma_task_atomic_cmp_swp_completion_cb_t error_task_completion_cb, uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_atomic_cmp_swp_allocate_init(struct doca_rdma * rdma,
                                                         struct doca_rdma_connection * rdma_connection,
                                                         struct doca_buf * dst_buf, struct doca_buf * result_buf,
                                                         uint64_t cmp_data, uint64_t swap_data,
                                                         union doca_data user_data,
                                                         struct doca_rdma_task_atomic_cmp_swp ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_atomic_cmp_swp_as_task(struct doca_rdma_task_atomic_cmp_swp * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_cmp_swp_set_dst_buf(struct doca_rdma_task_atomic_cmp_swp * task, struct doca_buf * dst_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_atomic_cmp_swp_get_dst_buf(const struct doca_rdma_task_atomic_cmp_swp * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_cmp_swp_set_result_buf(struct doca_rdma_task_atomic_cmp_swp * task,
                                                  struct doca_buf * result_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_atomic_cmp_swp_get_result_buf(const struct doca_rdma_task_atomic_cmp_swp * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_cmp_swp_set_cmp_data(struct doca_rdma_task_atomic_cmp_swp * task, uint64_t cmp_data);

DOCA_EXPERIMENTAL
uint64_t doca_rdma_task_atomic_cmp_swp_get_cmp_data(const struct doca_rdma_task_atomic_cmp_swp * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_cmp_swp_set_swap_data(struct doca_rdma_task_atomic_cmp_swp * task, uint64_t swap_data);

DOCA_EXPERIMENTAL
uint64_t doca_rdma_task_atomic_cmp_swp_get_swap_data(const struct doca_rdma_task_atomic_cmp_swp * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_cmp_swp_set_rdma_connection(struct doca_rdma_task_atomic_cmp_swp * task,
                                                       struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_atomic_cmp_swp_get_rdma_connection(
    const struct doca_rdma_task_atomic_cmp_swp * task);

struct doca_rdma_task_atomic_fetch_add;

typedef void (*doca_rdma_task_atomic_fetch_add_completion_cb_t)(struct doca_rdma_task_atomic_fetch_add * task,
                                                                union doca_data task_user_data,
                                                                union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_atomic_fetch_add_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_atomic_fetch_add_set_conf(
    struct doca_rdma * rdma, doca_rdma_task_atomic_fetch_add_completion_cb_t successful_task_completion_cb,
    doca_rdma_task_atomic_fetch_add_completion_cb_t error_task_completion_cb, uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_atomic_fetch_add_allocate_init(struct doca_rdma * rdma,
                                                           struct doca_rdma_connection * rdma_connection,
                                                           struct doca_buf * dst_buf, struct doca_buf * result_buf,
                                                           uint64_t add_data, union doca_data user_data,
                                                           struct doca_rdma_task_atomic_fetch_add ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_atomic_fetch_add_as_task(struct doca_rdma_task_atomic_fetch_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_fetch_add_set_dst_buf(struct doca_rdma_task_atomic_fetch_add * task,
                                                 struct doca_buf * dst_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_atomic_fetch_add_get_dst_buf(const struct doca_rdma_task_atomic_fetch_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_fetch_add_set_result_buf(struct doca_rdma_task_atomic_fetch_add * task,
                                                    struct doca_buf * result_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_atomic_fetch_add_get_result_buf(const struct doca_rdma_task_atomic_fetch_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_fetch_add_set_add_data(struct doca_rdma_task_atomic_fetch_add * task, uint64_t add_data);

DOCA_EXPERIMENTAL
uint64_t doca_rdma_task_atomic_fetch_add_get_add_data(const struct doca_rdma_task_atomic_fetch_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_atomic_fetch_add_set_rdma_connection(struct doca_rdma_task_atomic_fetch_add * task,
                                                         struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_atomic_fetch_add_get_rdma_connection(
    const struct doca_rdma_task_atomic_fetch_add * task);

struct doca_rdma_task_remote_net_sync_event_get;

typedef void (*doca_rdma_task_remote_net_sync_event_get_completion_cb_t)(
    struct doca_rdma_task_remote_net_sync_event_get * task, union doca_data task_user_data,
    union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_remote_net_sync_event_get_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_remote_net_sync_event_get_set_conf(
    struct doca_rdma * rdma, doca_rdma_task_remote_net_sync_event_get_completion_cb_t successful_task_completion_cb,
    doca_rdma_task_remote_net_sync_event_get_completion_cb_t error_task_completion_cb, uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_remote_net_sync_event_get_allocate_init(
    struct doca_rdma * rdma, struct doca_rdma_connection * rdma_connection,
    const struct doca_sync_event_remote_net * event, struct doca_buf * dst_buf, union doca_data user_data,
    struct doca_rdma_task_remote_net_sync_event_get ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_remote_net_sync_event_get_as_task(
    struct doca_rdma_task_remote_net_sync_event_get * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_get_set_sync_event(struct doca_rdma_task_remote_net_sync_event_get * task,
                                                             const struct doca_sync_event_remote_net * event);

DOCA_EXPERIMENTAL
const struct doca_sync_event_remote_net * doca_rdma_task_remote_net_sync_event_get_get_sync_event(
    const struct doca_rdma_task_remote_net_sync_event_get * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_get_set_dst_buf(struct doca_rdma_task_remote_net_sync_event_get * task,
                                                          struct doca_buf * dst_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_remote_net_sync_event_get_get_dst_buf(
    const struct doca_rdma_task_remote_net_sync_event_get * task);

DOCA_EXPERIMENTAL
uint32_t doca_rdma_task_remote_net_sync_event_get_get_result_len(
    const struct doca_rdma_task_remote_net_sync_event_get * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_get_set_rdma_connection(
    struct doca_rdma_task_remote_net_sync_event_get * task, struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_remote_net_sync_event_get_get_rdma_connection(
    const struct doca_rdma_task_remote_net_sync_event_get * task);

struct doca_rdma_task_remote_net_sync_event_notify_set;

typedef void (*doca_rdma_task_remote_net_sync_event_notify_set_completion_cb_t)(
    struct doca_rdma_task_remote_net_sync_event_notify_set * task, union doca_data task_user_data,
    union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_remote_net_sync_event_notify_set_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_remote_net_sync_event_notify_set_set_conf(
    struct doca_rdma * rdma,
    doca_rdma_task_remote_net_sync_event_notify_set_completion_cb_t successful_task_completion_cb,
    doca_rdma_task_remote_net_sync_event_notify_set_completion_cb_t error_task_completion_cb, uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_remote_net_sync_event_notify_set_allocate_init(
    struct doca_rdma * rdma, struct doca_rdma_connection * rdma_connection, struct doca_sync_event_remote_net * event,
    const struct doca_buf * src_buf, union doca_data user_data,
    struct doca_rdma_task_remote_net_sync_event_notify_set ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_remote_net_sync_event_notify_set_as_task(
    struct doca_rdma_task_remote_net_sync_event_notify_set * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_notify_set_set_sync_event(
    struct doca_rdma_task_remote_net_sync_event_notify_set * task, struct doca_sync_event_remote_net * event);

DOCA_EXPERIMENTAL
struct doca_sync_event_remote_net * doca_rdma_task_remote_net_sync_event_notify_set_get_sync_event(
    const struct doca_rdma_task_remote_net_sync_event_notify_set * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_notify_set_set_rdma_connection(
    struct doca_rdma_task_remote_net_sync_event_notify_set * task, struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_remote_net_sync_event_notify_set_get_rdma_connection(
    const struct doca_rdma_task_remote_net_sync_event_notify_set * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_notify_set_set_src_buf(
    struct doca_rdma_task_remote_net_sync_event_notify_set * task, const struct doca_buf * src_buf);

DOCA_EXPERIMENTAL
const struct doca_buf * doca_rdma_task_remote_net_sync_event_notify_set_get_src_buf(
    const struct doca_rdma_task_remote_net_sync_event_notify_set * task);

struct doca_rdma_task_remote_net_sync_event_notify_add;

typedef void (*doca_rdma_task_remote_net_sync_event_notify_add_completion_cb_t)(
    struct doca_rdma_task_remote_net_sync_event_notify_add * task, union doca_data task_user_data,
    union doca_data ctx_user_data);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_cap_task_remote_net_sync_event_notify_add_is_supported(const struct doca_devinfo * devinfo);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_remote_net_sync_event_notify_add_set_conf(
    struct doca_rdma * rdma,
    doca_rdma_task_remote_net_sync_event_notify_add_completion_cb_t successful_task_completion_cb,
    doca_rdma_task_remote_net_sync_event_notify_add_completion_cb_t error_task_completion_cb, uint32_t num_tasks);

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_task_remote_net_sync_event_notify_add_allocate_init(
    struct doca_rdma * rdma, struct doca_rdma_connection * rdma_connection, struct doca_sync_event_remote_net * event,
    struct doca_buf * result_buf, uint64_t add_data, union doca_data user_data,
    struct doca_rdma_task_remote_net_sync_event_notify_add ** task);

DOCA_EXPERIMENTAL
struct doca_task * doca_rdma_task_remote_net_sync_event_notify_add_as_task(
    struct doca_rdma_task_remote_net_sync_event_notify_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_notify_add_set_sync_event(
    struct doca_rdma_task_remote_net_sync_event_notify_add * task, struct doca_sync_event_remote_net * event);

DOCA_EXPERIMENTAL
struct doca_sync_event_remote_net * doca_rdma_task_remote_net_sync_event_notify_add_get_sync_event(
    const struct doca_rdma_task_remote_net_sync_event_notify_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_notify_add_set_result_buf(
    struct doca_rdma_task_remote_net_sync_event_notify_add * task, struct doca_buf * result_buf);

DOCA_EXPERIMENTAL
struct doca_buf * doca_rdma_task_remote_net_sync_event_notify_add_get_result_buf(
    const struct doca_rdma_task_remote_net_sync_event_notify_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_notify_add_set_add_data(
    struct doca_rdma_task_remote_net_sync_event_notify_add * task, uint64_t add_data);

DOCA_EXPERIMENTAL
uint64_t doca_rdma_task_remote_net_sync_event_notify_add_get_add_data(
    const struct doca_rdma_task_remote_net_sync_event_notify_add * task);

DOCA_EXPERIMENTAL
void doca_rdma_task_remote_net_sync_event_notify_add_set_rdma_connection(
    struct doca_rdma_task_remote_net_sync_event_notify_add * task, struct doca_rdma_connection * rdma_connection);

DOCA_EXPERIMENTAL
const struct doca_rdma_connection * doca_rdma_task_remote_net_sync_event_notify_add_get_rdma_connection(
    const struct doca_rdma_task_remote_net_sync_event_notify_add * task);

struct doca_dpa_completion;

DOCA_EXPERIMENTAL
doca_error_t doca_rdma_dpa_completion_attach(struct doca_rdma * rdma, struct doca_dpa_completion * dpa_comp);

#ifdef __cplusplus
}

#endif

#endif
