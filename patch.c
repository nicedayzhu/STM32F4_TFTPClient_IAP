From 3dd2617adcec55cb3cfa783ffb0c6b3a9cc0e51f Mon Sep 17 00:00:00 2001
From: Ben Wijen <ben@wijen.net>
Date: Mon, 23 Oct 2017 15:04:37 +0200
Subject: [PATCH] tftp: Add client functionality

* add helper functions
* add tftp_get/tftp_put
* rename files
---
 src/Filelists.mk                              |   4 +-
 .../tftp/{tftp_server.c => tftp_common.c}     | 112 +++++++++++++-----
 src/include/lwip/apps/tftp_client.h           |  40 +++++++
 .../apps/{tftp_server.h => tftp_common.h}     |  19 ++-
 src/include/lwip/apps/tftp_server.h           |  61 +---------
 5 files changed, 141 insertions(+), 95 deletions(-)
 rename src/apps/tftp/{tftp_server.c => tftp_common.c} (79%)
 create mode 100644 src/include/lwip/apps/tftp_client.h
 copy src/include/lwip/apps/{tftp_server.h => tftp_common.h} (82%)

diff --git a/src/Filelists.mk b/src/Filelists.mk
index 828b9f2a..7d43d364 100644
--- a/src/Filelists.mk
+++ b/src/Filelists.mk
@@ -181,8 +181,8 @@ MDNSFILES=$(LWIPDIR)/apps/mdns/mdns.c
 # NETBIOSNSFILES: NetBIOS name server
 NETBIOSNSFILES=$(LWIPDIR)/apps/netbiosns/netbiosns.c
 
-# TFTPFILES: TFTP server files
-TFTPFILES=$(LWIPDIR)/apps/tftp/tftp_server.c
+# TFTPFILES: TFTP client/server files
+TFTPFILES=$(LWIPDIR)/apps/tftp/tftp_common.c
 
 # MQTTFILES: MQTT client files
 MQTTFILES=$(LWIPDIR)/apps/mqtt/mqtt.c
diff --git a/src/apps/tftp/tftp_server.c b/src/apps/tftp/tftp_common.c
similarity index 79%
rename from src/apps/tftp/tftp_server.c
rename to src/apps/tftp/tftp_common.c
index e3f15124..c6cfc699 100644
--- a/src/apps/tftp/tftp_server.c
+++ b/src/apps/tftp/tftp_common.c
@@ -1,6 +1,6 @@
 /**
  *
- * @file tftp_server.c
+ * @file tftp_common.c
  *
  * @author   Logan Gunthorpe <logang@deltatee.com>
  *           Dirk Ziegelmeier <dziegel@gmx.de>
@@ -41,13 +41,13 @@
  */
 
 /**
- * @defgroup tftp TFTP server
+ * @defgroup tftp TFTP client/server
  * @ingroup apps
  *
- * This is simple TFTP server for the lwIP raw API.
+ * This is simple TFTP client/server for the lwIP raw API.
  */
 
-#include "lwip/apps/tftp_server.h"
+#include "lwip/apps/tftp_common.h"
 
 #if LWIP_UDP
 
@@ -114,6 +114,40 @@ close_handle(void)
   }
 }
 
+static struct pbuf*
+init_packet(int opcode, int extra, int size)
+{
+  struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(TFTP_HEADER_LENGTH + size), PBUF_RAM);
+  u16_t* payload;
+
+  if(p != NULL) {
+    payload = (u16_t*) p->payload;
+    payload[0] = PP_HTONS(opcode);
+    payload[1] = lwip_htons(extra);
+  }
+
+  return p;
+}
+
+static void
+send_request(const ip_addr_t *addr, u16_t port, int opcode, const char* fname, const char* mode) {
+  size_t fname_length = strlen(fname)+1;
+  size_t mode_length = strlen(mode)+1;
+  struct pbuf* p = init_packet(opcode, 0, -2 + fname_length + mode_length);
+  char* payload;
+
+  if(p == NULL) {
+    return;
+  }
+
+  payload = (char*) p->payload;
+  MEMCPY(payload+2,              fname, fname_length);
+  MEMCPY(payload+2+fname_length, mode,  mode_length);
+
+  udp_sendto(tftp_state.upcb, p, addr, port);
+  pbuf_free(p);
+}
+
 static void
 send_error(const ip_addr_t *addr, u16_t port, enum tftp_error code, const char *str)
 {
@@ -121,14 +155,12 @@ send_error(const ip_addr_t *addr, u16_t port, enum tftp_error code, const char *
   struct pbuf *p;
   u16_t *payload;
 
-  p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(TFTP_HEADER_LENGTH + str_length + 1), PBUF_RAM);
+  p = init_packet(TFTP_ERROR, code, str_length + 1);
   if (p == NULL) {
     return;
   }
 
   payload = (u16_t *) p->payload;
-  payload[0] = PP_HTONS(TFTP_ERROR);
-  payload[1] = lwip_htons(code);
   MEMCPY(&payload[2], str, str_length + 1);
 
   udp_sendto(tftp_state.upcb, p, addr, port);
@@ -136,25 +168,21 @@ send_error(const ip_addr_t *addr, u16_t port, enum tftp_error code, const char *
 }
 
 static void
-send_ack(u16_t blknum)
+send_ack(const ip_addr_t *addr, u16_t port, u16_t blknum)
 {
   struct pbuf *p;
-  u16_t *payload;
 
-  p = pbuf_alloc(PBUF_TRANSPORT, TFTP_HEADER_LENGTH, PBUF_RAM);
+  p = init_packet(TFTP_ACK, blknum, 0);
   if (p == NULL) {
     return;
   }
-  payload = (u16_t *) p->payload;
 
-  payload[0] = PP_HTONS(TFTP_ACK);
-  payload[1] = lwip_htons(blknum);
-  udp_sendto(tftp_state.upcb, p, &tftp_state.addr, tftp_state.port);
+  udp_sendto(tftp_state.upcb, p, addr, port);
   pbuf_free(p);
 }
 
 static void
-resend_data(void)
+resend_data(const ip_addr_t *addr, u16_t port)
 {
   struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, tftp_state.last_data->len, PBUF_RAM);
   if (p == NULL) {
@@ -166,12 +194,12 @@ resend_data(void)
     return;
   }
 
-  udp_sendto(tftp_state.upcb, p, &tftp_state.addr, tftp_state.port);
+  udp_sendto(tftp_state.upcb, p, addr, port);
   pbuf_free(p);
 }
 
 static void
-send_data(void)
+send_data(const ip_addr_t *addr, u16_t port)
 {
   u16_t *payload;
   int ret;
@@ -180,24 +208,22 @@ send_data(void)
     pbuf_free(tftp_state.last_data);
   }
 
-  tftp_state.last_data = pbuf_alloc(PBUF_TRANSPORT, TFTP_HEADER_LENGTH + TFTP_MAX_PAYLOAD_SIZE, PBUF_RAM);
+  tftp_state.last_data = init_packet(TFTP_DATA, tftp_state.blknum, TFTP_MAX_PAYLOAD_SIZE);
   if (tftp_state.last_data == NULL) {
     return;
   }
 
   payload = (u16_t *) tftp_state.last_data->payload;
-  payload[0] = PP_HTONS(TFTP_DATA);
-  payload[1] = lwip_htons(tftp_state.blknum);
 
   ret = tftp_state.ctx->read(tftp_state.handle, &payload[2], TFTP_MAX_PAYLOAD_SIZE);
   if (ret < 0) {
-    send_error(&tftp_state.addr, tftp_state.port, TFTP_ERROR_ACCESS_VIOLATION, "Error occured while reading the file.");
+    send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "Error occured while reading the file.");
     close_handle();
     return;
   }
 
   pbuf_realloc(tftp_state.last_data, (u16_t)(TFTP_HEADER_LENGTH + ret));
-  resend_data();
+  resend_data(addr, port);
 }
 
 static void
@@ -270,10 +296,10 @@ recv(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16
 
       if (opcode == PP_HTONS(TFTP_WRQ)) {
         tftp_state.mode_write = 1;
-        send_ack(0);
+        send_ack(addr, port, 0);
       } else {
         tftp_state.mode_write = 0;
-        send_data();
+        send_data(addr, port);
       }
 
       break;
@@ -302,7 +328,7 @@ recv(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16
           send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "error writing file");
           close_handle();
         } else {
-          send_ack(blknum);
+          send_ack(addr, port, blknum);
         }
 
         if (p->tot_len < TFTP_MAX_PAYLOAD_SIZE) {
@@ -312,7 +338,7 @@ recv(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16
         }
       } else if ((u16_t)(blknum + 1) == tftp_state.blknum) {
         /* retransmit of previous block, ack again (casting to u16_t to care for overflow) */
-        send_ack(blknum);
+        send_ack(addr, port, blknum);
       } else {
         send_error(addr, port, TFTP_ERROR_UNKNOWN_TRFR_ID, "Wrong block number");
       }
@@ -347,14 +373,20 @@ recv(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16
 
       if (!lastpkt) {
         tftp_state.blknum++;
-        send_data();
+        send_data(addr, port);
       } else {
         close_handle();
       }
 
       break;
     }
-
+    case PP_HTONS(TFTP_ERROR):
+      if (tftp_state.handle != NULL) {
+        pbuf_remove_header(p, TFTP_HEADER_LENGTH);
+        tftp_state.ctx->error(tftp_state.handle, sbuf[1], p->payload, p->len);
+        close_handle();
+      }
+      break;
     default:
       send_error(addr, port, TFTP_ERROR_ILLEGAL_OPERATION, "Unknown operation");
       break;
@@ -379,7 +411,7 @@ tftp_tmr(void *arg)
   if ((tftp_state.timer - tftp_state.last_pkt) > (TFTP_TIMEOUT_MSECS / TFTP_TIMER_MSECS)) {
     if ((tftp_state.last_data != NULL) && (tftp_state.retries < TFTP_MAX_RETRIES)) {
       LWIP_DEBUGF(TFTP_DEBUG | LWIP_DBG_STATE, ("tftp: timeout, retrying\n"));
-      resend_data();
+      resend_data(&tftp_state.addr, tftp_state.port);
       tftp_state.retries++;
     } else {
       LWIP_DEBUGF(TFTP_DEBUG | LWIP_DBG_STATE, ("tftp: timeout\n"));
@@ -389,7 +421,7 @@ tftp_tmr(void *arg)
 }
 
 /** @ingroup tftp
- * Initialize TFTP server.
+ * Initialize TFTP client/server.
  * @param ctx TFTP callback struct
  */
 err_t
@@ -422,7 +454,7 @@ tftp_init(const struct tftp_context *ctx)
 }
 
 /** @ingroup tftp
- * Deinitialize ("turn off") TFTP server.
+ * Deinitialize ("turn off") TFTP client/server.
  */
 void tftp_cleanup(void)
 {
@@ -432,4 +464,22 @@ void tftp_cleanup(void)
   memset(&tftp_state, 0, sizeof(tftp_state));
 }
 
+err_t
+tftp_get(void* handle, const ip_addr_t *addr, u16_t port, const char* fname, const char* mode) {
+  tftp_state.handle = handle;
+  tftp_state.blknum = 1;
+  tftp_state.mode_write = 1; // We want to receive data
+  send_request(addr, port, TFTP_RRQ, fname, mode);
+  return ERR_OK;
+}
+
+err_t
+tftp_put(void* handle, const ip_addr_t *addr, u16_t port, const char* fname, const char* mode) {
+  tftp_state.handle = handle;
+  tftp_state.blknum = 1;
+  tftp_state.mode_write = 0; // We want to send data
+  send_request(addr, port, TFTP_WRQ, fname, mode);
+  return ERR_OK;
+}
+
 #endif /* LWIP_UDP */
diff --git a/src/include/lwip/apps/tftp_client.h b/src/include/lwip/apps/tftp_client.h
new file mode 100644
index 00000000..c241ddba
--- /dev/null
+++ b/src/include/lwip/apps/tftp_client.h
@@ -0,0 +1,40 @@
+/**
+ *
+ * @file tftp_client.h
+ * TFTP client header
+ *
+ */
+
+/* 
+ * Redistribution and use in source and binary forms, with or without
+ * modification,are permitted provided that the following conditions are met:
+ *
+ * 1. Redistributions of source code must retain the above copyright notice,
+ *    this list of conditions and the following disclaimer.
+ * 2. Redistributions in binary form must reproduce the above copyright notice,
+ *    this list of conditions and the following disclaimer in the documentation
+ *    and/or other materials provided with the distribution.
+ * 3. The name of the author may not be used to endorse or promote products
+ *    derived from this software without specific prior written permission.
+ *
+ * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
+ * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
+ * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
+ * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
+ * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
+ * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
+ * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
+ * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
+ * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
+ * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
+ *
+ * This file is part of the lwIP TCP/IP stack.
+ *
+ */
+
+#ifndef LWIP_HDR_APPS_TFTP_CLIENT_H
+#define LWIP_HDR_APPS_TFTP_CLIENT_H
+
+#include "lwip/apps/tftp_common.h"
+
+#endif /* LWIP_HDR_APPS_TFTP_CLIENT_H */
diff --git a/src/include/lwip/apps/tftp_server.h b/src/include/lwip/apps/tftp_common.h
similarity index 82%
copy from src/include/lwip/apps/tftp_server.h
copy to src/include/lwip/apps/tftp_common.h
index 0a7fbee0..36653f26 100644
--- a/src/include/lwip/apps/tftp_server.h
+++ b/src/include/lwip/apps/tftp_common.h
@@ -1,6 +1,6 @@
 /**
  *
- * @file tftp_server.h
+ * @file tftp_common.h
  *
  * @author   Logan Gunthorpe <logang@deltatee.com>
  *
@@ -38,12 +38,13 @@
  *
  */
 
-#ifndef LWIP_HDR_APPS_TFTP_SERVER_H
-#define LWIP_HDR_APPS_TFTP_SERVER_H
+#ifndef LWIP_HDR_APPS_TFTP_COMMON_H
+#define LWIP_HDR_APPS_TFTP_COMMON_H
 
 #include "lwip/apps/tftp_opts.h"
 #include "lwip/err.h"
 #include "lwip/pbuf.h"
+#include "lwip/ip_addr.h"
 
 #ifdef __cplusplus
 extern "C" {
@@ -83,13 +84,23 @@ struct tftp_context {
    * @returns &gt;= 0: Success; &lt; 0: Error
    */
   int (*write)(void* handle, struct pbuf* p);
+  /**
+   * Error response
+   * @param handle File handle set by tftp_get/tftp_put
+   * @param err error code from server
+   * @param msg error message from server
+   * @param size size of msg
+   */
+  void (*error)(void* handle, int err, const char* msg, int size);
 };
 
 err_t tftp_init(const struct tftp_context* ctx);
 void tftp_cleanup(void);
+err_t tftp_get(void* handle, const ip_addr_t *addr, u16_t port, const char* fname, const char* mode);
+err_t tftp_put(void* handle, const ip_addr_t *addr, u16_t port, const char* fname, const char* mode);
 
 #ifdef __cplusplus
 }
 #endif
 
-#endif /* LWIP_HDR_APPS_TFTP_SERVER_H */
+#endif /* LWIP_HDR_APPS_TFTP_COMMON_H */
diff --git a/src/include/lwip/apps/tftp_server.h b/src/include/lwip/apps/tftp_server.h
index 0a7fbee0..16168c8e 100644
--- a/src/include/lwip/apps/tftp_server.h
+++ b/src/include/lwip/apps/tftp_server.h
@@ -1,13 +1,7 @@
 /**
  *
  * @file tftp_server.h
- *
- * @author   Logan Gunthorpe <logang@deltatee.com>
- *
- * @brief    Trivial File Transfer Protocol (RFC 1350)
- *
- * Copyright (c) Deltatee Enterprises Ltd. 2013
- * All rights reserved.
+ * TFTP server header
  *
  */
 
@@ -34,62 +28,13 @@
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
- * Author: Logan Gunthorpe <logang@deltatee.com>
+ * This file is part of the lwIP TCP/IP stack.
  *
  */
 
 #ifndef LWIP_HDR_APPS_TFTP_SERVER_H
 #define LWIP_HDR_APPS_TFTP_SERVER_H
 
-#include "lwip/apps/tftp_opts.h"
-#include "lwip/err.h"
-#include "lwip/pbuf.h"
-
-#ifdef __cplusplus
-extern "C" {
-#endif
-
-/** @ingroup tftp
- * TFTP context containing callback functions for TFTP transfers
- */
-struct tftp_context {
-  /**
-   * Open file for read/write.
-   * @param fname Filename
-   * @param mode Mode string from TFTP RFC 1350 (netascii, octet, mail)
-   * @param write Flag indicating read (0) or write (!= 0) access
-   * @returns File handle supplied to other functions
-   */
-  void* (*open)(const char* fname, const char* mode, u8_t write);
-  /**
-   * Close file handle
-   * @param handle File handle returned by open()
-   */
-  void (*close)(void* handle);
-  /**
-   * Read from file 
-   * @param handle File handle returned by open()
-   * @param buf Target buffer to copy read data to
-   * @param bytes Number of bytes to copy to buf
-   * @returns &gt;= 0: Success; &lt; 0: Error
-   */
-  int (*read)(void* handle, void* buf, int bytes);
-  /**
-   * Write to file
-   * @param handle File handle returned by open()
-   * @param pbuf PBUF adjusted such that payload pointer points
-   *             to the beginning of write data. In other words,
-   *             TFTP headers are stripped off.
-   * @returns &gt;= 0: Success; &lt; 0: Error
-   */
-  int (*write)(void* handle, struct pbuf* p);
-};
-
-err_t tftp_init(const struct tftp_context* ctx);
-void tftp_cleanup(void);
-
-#ifdef __cplusplus
-}
-#endif
+#include "lwip/apps/tftp_common.h"
 
 #endif /* LWIP_HDR_APPS_TFTP_SERVER_H */
-- 
2.18.0

