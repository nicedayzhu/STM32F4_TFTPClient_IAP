/**
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of and a contribution to the lwIP TCP/IP stack.
 *
 * Credits go to Adam Dunkels (and the current maintainers) of this software.
 *
 * Christiaan Simons rewrote this file to get a more stable echo application.
 *
 **/

 /* This file was modified by ST */


#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "usart.h"

#if LWIP_TCP

static struct tcp_pcb *tcp_echoserver_pcb;

/* ECHO protocol states */
enum tcp_echoserver_states
{
  ES_NONE = 0,
  ES_ACCEPTED,
  ES_RECEIVED,
  ES_CLOSING
};

/* structure for maintaing connection infos to be passed as argument 
   to LwIP callbacks*/
struct tcp_echoserver_struct
{
  u8_t state;             /* current connection state */
  u8_t retries;
  struct tcp_pcb *pcb;    /* pointer on the current tcp_pcb */
  struct pbuf *p;         /* pointer on the received/to be transmitted pbuf */
};


static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void tcp_echoserver_error(void *arg, err_t err);
static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb);
static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);
static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);


/**
  * @brief  Initializes the tcp echo server
  * @param  None
  * @retval None
  */
void tcp_echoserver_init(void)
{
  /* create new tcp pcb */
  tcp_echoserver_pcb = tcp_new();

  if (tcp_echoserver_pcb != NULL)
  {
    err_t err;
    
    /* bind echo_pcb to port 7 (ECHO protocol) */
    err = tcp_bind(tcp_echoserver_pcb, IP_ADDR_ANY, 7);
    
    if (err == ERR_OK)
    {
      /* start tcp listening for echo_pcb */
      tcp_echoserver_pcb = tcp_listen(tcp_echoserver_pcb);
      printf("开始监听 \r\n");
      /* initialize LwIP tcp_accept callback function */
      tcp_accept(tcp_echoserver_pcb, tcp_echoserver_accept);
      printf("挂载客户端连接回调函数 \r\n");
    }
    else 
    {
      /* deallocate the pcb */
      memp_free(MEMP_TCP_PCB, tcp_echoserver_pcb);
      printf("TCP PCB 内存申请失败 \r\n");
    }
  }
}

/**
  * @brief  This function is the implementation of tcp_accept LwIP callback
  * @param  arg: not used
  * @param  newpcb: pointer on tcp_pcb struct for the newly created tcp connection
  * @param  err: not used 
  * @retval err_t: error status
  */
static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  err_t ret_err;
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(err);

  /* set priority for the newly accepted tcp connection newpcb */
  tcp_setprio(newpcb, TCP_PRIO_MIN);
  printf("收到客户端连接请求，设置刚连接的客户端为最低优先级 \r\n");
    
  uint8_t iptxt[20];
  sprintf((char *)iptxt, "%s", ip4addr_ntoa((const ip4_addr_t *)&newpcb->remote_ip));
  printf ("客户端 IP address: %s\r\n", iptxt); 
    
  /* allocate structure es to maintain tcp connection informations */
  es = (struct tcp_echoserver_struct *)mem_malloc(sizeof(struct tcp_echoserver_struct));
  if (es != NULL)
  {
    es->state = ES_ACCEPTED;
    es->pcb = newpcb;
    es->retries = 0;
    es->p = NULL;
      
    printf("为新连接的客户端挂载需要的回调函数及 调用参数 \r\n");
      
    /* pass newly allocated es structure as argument to newpcb */
    tcp_arg(newpcb, es);
    
    /* initialize lwip tcp_recv callback function for newpcb  */ 
    tcp_recv(newpcb, tcp_echoserver_recv);
    
    /* initialize lwip tcp_err callback function for newpcb  */
    tcp_err(newpcb, tcp_echoserver_error);
    
    /* initialize lwip tcp_poll callback function for newpcb */
    tcp_poll(newpcb, tcp_echoserver_poll, 0);  
    ret_err = ERR_OK;
  }
  else
  {
    /*  close tcp connection */
    tcp_echoserver_connection_close(newpcb, es);
    printf("tcp_echoserver_struct 内存申请失败 关闭连接 \r\n");
    /* return memory error */
    ret_err = ERR_MEM;
  }
  return ret_err;  
}


/**
  * @brief  This function is the implementation for tcp_recv LwIP callback
  * @param  arg: pointer on a argument for the tcp_pcb connection
  * @param  tpcb: pointer on the tcp_pcb connection
  * @param  pbuf: pointer on the received pbuf
  * @param  err: error information regarding the reveived pbuf
  * @retval err_t: error code
  */
static err_t tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  struct tcp_echoserver_struct *es;
  err_t ret_err;

  LWIP_ASSERT("arg != NULL",arg != NULL);
  printf("收到客户端数据\r\n");
  es = (struct tcp_echoserver_struct *)arg;
  
  /* if we receive an empty tcp frame from client => close connection */
  if (p == NULL)
  {
    printf("收到断开连接请求 \r\n");
    /* remote host closed connection */
    es->state = ES_CLOSING;
    if(es->p == NULL)
    {
       /* we're done sending, close connection */
       tcp_echoserver_connection_close(tpcb, es);
    }
    else
    {
       printf("发送的数据还未发送完成 \r\n");
      /* we're not done yet */
      /* acknowledge received packet */
      tcp_sent(tpcb, tcp_echoserver_sent);
      printf("装载发送完成回调函数 \r\n");
      /* send remaining data*/
      tcp_echoserver_send(tpcb, es);
    }
    ret_err = ERR_OK;
  }   
  /* else : a non empty frame was received from client but for some reason err != ERR_OK */
  else if(err != ERR_OK)
  {
    /* free received pbuf*/
    if (p != NULL)
    {
      es->p = NULL;
      pbuf_free(p);
    }
    ret_err = err;
  }
  else if(es->state == ES_ACCEPTED)
  {
    /* first data chunk in p->payload */
    es->state = ES_RECEIVED;
    
    /* store reference to incoming pbuf (chain) */
    es->p = p;
    
    /* initialize LwIP tcp_sent callback function */
    tcp_sent(tpcb, tcp_echoserver_sent);
    printf("设备刚连接，挂载刚才接收到的数据，设置发送完成回调 \r\n");
    /* send back the received data (echo) */
    tcp_echoserver_send(tpcb, es);
    
    ret_err = ERR_OK;
  }
  else if (es->state == ES_RECEIVED)
  {
    /* more data received from client and previous data has been already sent*/
    if(es->p == NULL)
    {
      es->p = p;
      printf("接收到的数据直接回传 \r\n");
      /* send back received data */
      tcp_echoserver_send(tpcb, es);
    }
    else
    {
      struct pbuf *ptr;
      printf("上次的数据还未发送完成，把新数据拼接在后面 \r\n");
      /* chain pbufs to the end of what we recv'ed previously  */
      ptr = es->p;
      pbuf_chain(ptr,p);
    }
    ret_err = ERR_OK;
  }
  else if(es->state == ES_CLOSING)
  {
    printf("当前已经是关闭连接了，但还是受到数据 \r\n");
    /* odd case, remote side closing twice, trash data */
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  else
  {
    /* unkown es->state, trash data  */
    tcp_recved(tpcb, p->tot_len);
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  return ret_err;
}

/**
  * @brief  This function implements the tcp_err callback function (called
  *         when a fatal tcp_connection error occurs. 
  * @param  arg: pointer on argument parameter 
  * @param  err: not used
  * @retval None
  */
static void tcp_echoserver_error(void *arg, err_t err)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(err);
  printf("错误 ： %d \r\n",err);
  es = (struct tcp_echoserver_struct *)arg;
  if (es != NULL)
  {
    /*  free es structure */
    mem_free(es);
  }
}

/**
  * @brief  This function implements the tcp_poll LwIP callback function
  * @param  arg: pointer on argument passed to callback
  * @param  tpcb: pointer on the tcp_pcb for the current tcp connection
  * @retval err_t: error code
  */
static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb)
{
  err_t ret_err;
  struct tcp_echoserver_struct *es;

  es = (struct tcp_echoserver_struct *)arg;
  if (es != NULL)
  {
    if (es->p != NULL)
    {
      tcp_sent(tpcb, tcp_echoserver_sent);
      /* there is a remaining pbuf (chain) , try to send data */
      tcp_echoserver_send(tpcb, es);
    }
    else
    {
      /* no remaining pbuf (chain)  */
      if(es->state == ES_CLOSING)
      {
        /*  close tcp connection */
        tcp_echoserver_connection_close(tpcb, es);
      }
    }
    ret_err = ERR_OK;
  }
  else
  {
    /* nothing to be done */
    tcp_abort(tpcb);
    ret_err = ERR_ABRT;
  }
  return ret_err;
}

/**
  * @brief  This function implements the tcp_sent LwIP callback (called when ACK
  *         is received from remote host for sent data) 
  * @param  None
  * @retval None
  */
static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(len);

  es = (struct tcp_echoserver_struct *)arg;
  es->retries = 0;
  
  if(es->p != NULL)
  {
    /* still got pbufs to send */
    tcp_sent(tpcb, tcp_echoserver_sent);
    tcp_echoserver_send(tpcb, es);
  }
  else
  {
    /* if no more data to send and client closed connection*/
    if(es->state == ES_CLOSING)
      tcp_echoserver_connection_close(tpcb, es);
  }
  return ERR_OK;
}


/**
  * @brief  This function is used to send data for tcp connection
  * @param  tpcb: pointer on the tcp_pcb connection
  * @param  es: pointer on echo_state structure
  * @retval None
  */
static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  struct pbuf *ptr;
  err_t wr_err = ERR_OK;
  uint16_t Count ;
  printf("发送数据的总长度 : %ld \r\n",es->p->tot_len);
  printf("发送数据： \r\n");
  while ((wr_err == ERR_OK) &&
         (es->p != NULL) && 
         (es->p->len <= tcp_sndbuf(tpcb)))
  {
    
    /* get pointer on pbuf from es structure */
    ptr = es->p; //得到当前需要发送的数据缓存 pbuf

    for( Count =0 ; Count < ptr->len; Count++ )
    {
      printf("0X%02X ",((uint8_t *)ptr->payload)[Count]);
    }
    /* enqueue data for transmission */
    wr_err = tcp_write(tpcb, ptr->payload, ptr->len, TCP_WRITE_FLAG_COPY);  //发送
    
    if (wr_err == ERR_OK)
    {
      u16_t plen;
      u8_t freed;

      plen = ptr->len; //得到当前节点的数据长度
     
      /* continue with next pbuf in chain (if any) */
      es->p = ptr->next; //得到链表的下一个节点
      
      if(es->p != NULL)  //如果节点不为空
      {
        /* increment reference count for es->p */
        pbuf_ref(es->p); //成员 ref 的值加1  当收到数据时，pbuf成员ref的值为1 ，在pbuf_free（）函数中是对成员ref减一 如果结果为0则释放此节点内存并进入下一个节点  否则只是把成员ref的值减一
      }
      
     /* chop first pbuf from chain */
      do
      {
        /* try hard to free pbuf */
        freed = pbuf_free(ptr);  //每执行一个 pbuf中的成员ref的值减一  当它这个节点没有释放时，返回一直为0
      }
      while(freed == 0);  //执行直到释放这个节点内存为止  pbuf_free（）函数的返回值表示释放的内存的节点数
     /* we can read more data now */
     tcp_recved(tpcb, plen);   //增加可以接收数据的大小
   }
   else if(wr_err == ERR_MEM)
   {
      /* we are low on memory, try later / harder, defer to poll */
     es->p = ptr; //重发
   }
   else
   {
     /* other problem ?? */
   }
  }
  printf("\r\n");
}

/**
  * @brief  This functions closes the tcp connection
  * @param  tcp_pcb: pointer on the tcp connection
  * @param  es: pointer on echo_state structure
  * @retval None
  */
static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  
  /* remove all callbacks */
  tcp_arg(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);
  
  /* delete es structure */
  if (es != NULL)
  {
    mem_free(es);
  }  
  
  /* close tcp connection */
  tcp_close(tpcb);
  printf("已关闭连接 \r\n");
}

#endif /* LWIP_TCP */
