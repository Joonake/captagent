/*
 * captagent - Homer capture agent. Modular
 * dpdk_demo - DPDK demo test
 *
 * Michele Campus <fci1908@gmail.com>
 * (C) QXIP BV 2012-2018 (http://qxip.net)
 * 
 * Homer capture agent is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version
 *
 * Homer capture agent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <rte_eal.h>
#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_log.h>

// Macro for printing using rte_log
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
// Mem buffer dimension
#define NUM_MBUFS 8191
// Mem buffer cache dimension
#define MBUF_CACHE_SIZE 250

// Global value for port init
#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

// Value for lcore
#define BURST_SIZE 32

// value for lcore_main
static u_int8_t forwarding_lcore = 1;

/* Check link status */
static int check_link_status(u_int16_t nb_ports)
{
  struct rte_eth_link link;
  u_int8_t port;

  for(port = 0; port < nb_ports; port++) {

    ret_eth_link_get(port, &link);
    if(link.link_status == ETH_LINK_DOWN) {
      RTE_LOG(INFO, APP, "Port %u link is down\n", port);
      return -1;
    }
    RTE_LOG(INFO, APP, "Port %u link is up and speed %u\n", port, link.link_speed);
  }
  
  return 0;
}


/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
int lcore_main(void *arg)
{
  unsigned int lcore_id;
  uint8_t port;
  const u_int8_t nb_ports;
  int ret;

  lcore_id = rte_lcore_id();      // cores
  nb_ports = rte_eth_dev_count(); // ports

  if(lcore_id != forwarding_lcore)
    RTE_LOG(INFO, APP, "lcore %u exiting\n", lcore_id);

  /* Check port link status  */
  ret = check_link_status(nb_ports);
  if(ret < 0)
    RTE_LOG(WARNING, APP, "Some ports are down\n", lcore_id);

  while(!quit) {
    /* Receive pkts on a port
       The mapping is 0 -> 1, 1 -> 0, 2 -> 3, 3 -> 2, etc
    */
    for(port = 0; port < nb_ports; port++) {
      struct rte_mbuf *bufs[BURST_SIZE];
      const uint16_t nb_rx;
      const uint16_t nb_tx;

      /* Get burst of RX packets for first port of pair */
      nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
      if(unlikely(nb_rx == 0))
	continue;

      /* Send burst of TX packets to second port of pair */
      const uint16_t nb_tx = rte_eth_tx_burst(port ^ 1, 0, bufs, nb_rx);

      /* Free any unsent pkts */
      if(unlikely(nb_tx < nb_rx)) {
	uint16_t buf;
	for(buf = nb_tx; buf < nb_rx; buf++)
	  rte_pktmbuf_free(bufs[buf]);
      }
    }
  }
}

/**
   Initializes a given port using global settings and with the RX buffers
   coming from the mbuf_pool.
*/
static inline int
port_init(uint8_t port, struct rte_mempool *mbuf_pool)
{
  
  // struct for port init
  const struct rte_eth_conf port_conf = {
    .rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
  };
  
  const uint16_t rx_rings = 1, tx_rings = 1;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval;
  uint16_t q;

  if(port >= rte_eth_dev_count())
    return -1;

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port,
				 rx_rings,
				 tx_rings,
				 &port_conf);
  if(retval != 0)
    return retval;

  /**
     Check that numbers of Rx and Tx descriptors satisfy descriptors limits from the ethernet device information, otherwise adjust them to boundaries
  */
  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0)
    return retval;

  /**
     This functions allocates a contiguous block of memory for nb_rxd receive descriptors from a memory zone associated with socket_id.
     Initializes each receive descriptor with a network buffer allocated from the memory pool mbuf_pool
  */
  for(q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				    rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if(retval < 0)
      return retval;
  }
  /**
     This functions allocates a contiguous block of memory for nb_txd sender descriptors from a memory zone associated with socket_id.
     Allocate and set up a transmit queue for an Ethernet device.
  */
  for(q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				    rte_eth_dev_socket_id(port), NULL);
    if(retval < 0)
      return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if(retval < 0)
    return retval;

  /* Display the port MAC address. */
  struct ether_addr addr;
  rte_eth_macaddr_get(port, &addr);
  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
	 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
	 (unsigned)port,
	 addr.addr_bytes[0], addr.addr_bytes[1],
	 addr.addr_bytes[2], addr.addr_bytes[3],
	 addr.addr_bytes[4], addr.addr_bytes[5]);

  /* Enable RX in promiscuous mode for the Ethernet device. */
  /* rte_eth_promiscuous_enable(port); */

  /**
     CORE LOOP:
     Launch a function on all lcores
     Check that each SLAVE lcore is in a WAIT state, then call rte_eal_remote_launch() for each lcore
     NOTE: SKIP_MASTER or CALL_MASTER
  */
  rte_eal_mp_remote_launch(lcore_main, NULL, SKIP_MASTER);
  // wait for job finish on each core
  rte_eal_mp_wait_lcore();

  

  return 0;
}

/**
   Print statistics on exit
*/
static void print_stats(void)
{
  struct rte_eth_stats stats;
  u_int8_t nb_port = rte_eth_dev_count();
  u_int8_t i;

  for (i = 0; i < nb_port; i++) {
    printf("Stats for port %u\n", port);
    rte_eth_stats_get(port, &stats);
    printf("Rx: %9llu Tx: %9llu Dropped: %9llu\n",
	   stats.ipackets, stats.opackets, stats.imissed);
  }
}

/**
   signal handler
*/
static volatile sig_atomic quit = false;

static void signal_handler(int sig) {
  
  if(sig == SIGINT || sig == SIGTERM) {
    printf("Catching %d signal! Going to quit...\n");
    quit = true;
    print_stats();
  }
}
  

int main(int argc, char *argv[])
{
  int ret;
  u_int8_t n_ports, port_id;
  struct rte_mempool *mbuf_pool;

    
  /* EAL (Environment Abstraction Layer) init */
  ret = rte_eal_init(argc, argv);
  if(ret < 0)
    rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
  
  argc -= ret;
  argv += ret;

  quit = false;
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* Get number of ports
     check thet there is an even number of ports to send/receive on
  */
  n_ports = rte_eth_dev_count();
  if(n_ports < 2 || (nb_ports & 1))
    rte_exit(EXIT_FAILURE, "Invalid port number\n");

  RTE_LOG(INFO, APP, "Number of ports: %u\n", n_ports);

  /* Creates a new mempool in memory to hold the mbufs */
  mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * n_ports,
				      MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if(mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Fail to create mbuf pool\n");

  /* Initialize all ports */
  for(portid = 0; portid < n_ports; portid++)
    if(port_init(portid, mbuf_pool) != 0)
      rte_exit(EXIT_FAILURE, "Fail to init port %"PRIu8 "\n", portid);
  
  return 0;
}
