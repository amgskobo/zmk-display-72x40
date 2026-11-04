/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zmk/event_manager.h>
#include <zmk-display-72x40/power_state.h>
static atomic_t reports;
static atomic_t d72x40_power_sync_left;
static atomic_t pause_read;
K_SEM_DEFINE(paused, 0, 1);
K_SEM_DEFINE(resume, 0, 1);
static void rendezvous(const atomic_t *counter) {
 if (counter == &d72x40_power_sync_left && atomic_cas(&pause_read, 1, 0)) {
  k_sem_give(&paused);
  __ASSERT_NO_MSG(k_sem_take(&resume, K_SECONDS(1)) == 0);
 }
}
static atomic_val_t observed_get(const atomic_t *counter) {
 atomic_val_t old = atomic_get(counter); rendezvous(counter); return old;
}
static atomic_val_t observed_dec(atomic_t *counter) __attribute__((unused));
static atomic_val_t observed_dec(atomic_t *counter) {
 atomic_val_t old = atomic_dec(counter); rendezvous(counter); return old;
}
#define atomic_get observed_get
#define atomic_dec observed_dec
#include DISPLAY_POWER_SOURCE
#undef atomic_get
#undef atomic_dec
static int raise_zmk_display_72x40_power_state_changed(struct zmk_display_72x40_power_state_changed event) {
 __ASSERT_NO_MSG(event.source == 255 && event.powered);
 atomic_inc(&reports); return 0;
}
static void link(bool connected) {
 const zmk_event_t event = {connected};
 __ASSERT_NO_MSG(d72x40_power_listener(&event) == 0);
}
int main(void) {
 k_msleep(50); __ASSERT_NO_MSG(atomic_get(&d72x40_power_known) == 1);
 link(false);
 atomic_set(&pause_read, 1);
 k_work_reschedule_for_queue(&k_sys_work_q, &d72x40_power_sync_work, K_NO_WAIT);
 __ASSERT_NO_MSG(k_sem_take(&paused, K_SECONDS(1)) == 0);
 const int before = atomic_get(&reports);
 link(true);
 __ASSERT_NO_MSG(atomic_get(&d72x40_power_sync_left) == 4);
 k_sem_give(&resume);
 k_msleep(10);
 __ASSERT(atomic_get(&d72x40_power_sync_left) == 4, "old callback erased reconnect retries");
 for (int i = 0; i < 4; i++) {
  for (int wait = 0; wait < 1700 && atomic_get(&reports) < before + i + 1; wait++) { k_msleep(1); }
  __ASSERT_NO_MSG(atomic_get(&reports) == before + i + 1);
  printk("power retry %d at %lldms, budget=%ld\n", i + 1, k_uptime_get(), (long)atomic_get(&d72x40_power_sync_left));
 }
 k_msleep(20); __ASSERT_NO_MSG(atomic_get(&d72x40_power_sync_left) == 0);
 printk("PASS: reconnect retains four retries across an old running callback\n");
 exit(0); return 0;
}
