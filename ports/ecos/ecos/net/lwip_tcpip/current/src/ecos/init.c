/*
 * init.c - helper code for initing applications that use lwIP		
 */

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/memp.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"

#if LWIP_DHCP
#include "lwip/dhcp.h"
#endif

#if LWIP_SLIP
#include "netif/slipif.h"
#endif

#include "netif/etharp.h"

#include <cyg/io/eth/eth_drv.h>
#include <cyg/io/eth/netdev.h>

// Define table boundaries
CYG_HAL_TABLE_BEGIN(__NETDEVTAB__, netdev);
CYG_HAL_TABLE_END(__NETDEVTAB_END__, netdev);

void inline IP_ADDR(struct ip_addr *ipaddr, char a, char b, char c, char d)
{
	IP4_ADDR(ipaddr,a,b,c,d);
}


void tcpip_init_done(void * arg)
{
	sys_sem_t *sem = arg;
	sys_sem_signal(*sem);
}

struct netif mynetif;
static void ecosglue_init(void);
void lwip_set_addr(struct netif *netif);
#if PPP_SUPPORT
void pppMyCallback(void *a , int e)
{
	diag_printf("callback %d \n",e);
}
#endif
/*
 * Called by the eCos application at startup
 * wraps various init calls
 */
void lwip_init(void)
{
	sys_sem_t sem;
	
	sys_init();	/* eCos specific initialization */
	mem_init();	/* heap based memory allocator */
	memp_init();	/* pool based memory allocator */
	pbuf_init();	/* packet buffer allocator */
	netif_init();	/* netif layer */
	
	/* Start the stack.It will spawn a new dedicated thread */
	sem = sys_sem_new(0);
	tcpip_init(tcpip_init_done,&sem);
	sys_sem_wait(sem);
	sys_sem_free(sem);
#if LWIP_SLIP	
	lwip_set_addr(&mynetif);
	slipif_init(&mynetif);
#elif PPP_SUPPORT
	pppInit();
#if PAP_SUPPORT || CHAP_SUPPORT
	pppSetAuth("ecos", "picula");
#endif
	pppOpen(0, pppMyCallback, NULL);
#else	
	ecosglue_init();		
#endif	
}

void lwip_set_addr(struct netif *netif)
{
	struct ip_addr ipaddr, netmask, gw;

	IP_ADDR(&gw, CYGPKG_LWIP_SERV_ADDR);
	IP_ADDR(&ipaddr, CYGPKG_LWIP_MY_ADDR);
	IP_ADDR(&netmask, CYGPKG_LWIP_NETMASK);
	netif_set_addr(netif, &ipaddr, &netmask, &gw);
	netif->next = NULL;
	netif_list = netif;
	
	netif->input = tcpip_input;
}

//io eth stuff

cyg_sem_t delivery;

void lwip_dsr_stuff(void)
{
  cyg_semaphore_post(&delivery);
}
//Input thread signalled by DSR calls deliver() on low level drivers
static void
input_thread(void *arg)
{
  cyg_netdevtab_entry_t *t;

  for (;;) {
    cyg_semaphore_wait(&delivery);

    for (t = &__NETDEVTAB__[0]; t != &__NETDEVTAB_END__; t++) {
      struct eth_drv_sc *sc = (struct eth_drv_sc *)t->device_instance;
      if (sc->state & ETH_DRV_NEEDS_DELIVERY) {
	sc->state &= ~ETH_DRV_NEEDS_DELIVERY;
	(sc->funs->deliver) (sc);
      }
    }
  }

}

// Initialize all network devices
static void
init_hw_drivers(void)
{
  cyg_netdevtab_entry_t *t;

  for (t = &__NETDEVTAB__[0]; t != &__NETDEVTAB_END__; t++) {
    if (t->init(t)) {
      t->status = CYG_NETDEVTAB_STATUS_AVAIL;
    } else {
      // What to do if device init fails?
      t->status = 0;		// Device not [currently] available
    }
  }
}

static void
arp_timer(void *arg)
{
  etharp_tmr();
  sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler) arp_timer, NULL);
}


static void
ecosglue_init(void)
{
  init_hw_drivers();
  sys_thread_new(input_thread, (void*)0,7);
  etharp_init();
  sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler) arp_timer, NULL);
}