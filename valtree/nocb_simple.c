/*
 * Litmus test for the RCU callback offloading mechanism (NOCB_CPU), v3.19.
 *
 * By default, the RCU-bh kthread are disabled by default for
 * faster results. If desired, they can be enabled with -DENABLE_RCU_BH.
 * This test needs to be compiled with the -DCONFIG_RCU_NOCB_CPU and 
 * -DCONFIG_RCU_NOCB_CPU_ZERO flags. There are also two added bug 
 * injections which can be enabled with -DFORCE_FAILURE_NOCB_[1,2].
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Author: Michalis Kokologiannakis <mixaskok@gmail.com>
 */

#include "fake_defs.h"
#include "fake_sync.h"
#include <linux/rcupdate.h>
#include <update.c>
#include "tree.c"
#include "fake_sched.h"

/* Memory de-allocation boils down to a call to free */
void kfree(const void *p)
{
	free((void *) p);
}

int cpu0 = 0;
int cpu1 = 1;

/* Code under test */

int r_x;
int r_y;

int x;
int y;

void *thread_reader(void *arg)
{
	set_cpu(cpu1);
	fake_acquire_cpu(get_cpu());

	rcu_read_lock();	
        r_x = x; 
	do_IRQ();
#ifdef FORCE_FAILURE_4
	rcu_idle_enter();
	rcu_idle_exit();
#endif
#ifdef FORCE_FAILURE_1
	cond_resched();
	do_IRQ();
#endif
	r_y = y; 
	rcu_read_unlock();
#if !defined(FORCE_FAILURE_1) && !defined(FORCE_FAILURE_4) &&	\
    !defined(FORCE_FAILURE_5)
	cond_resched();
	do_IRQ();
#endif

	fake_release_cpu(get_cpu());	
	return NULL;
}

void *thread_update(void *arg)
{
	set_cpu(cpu0);
	fake_acquire_cpu(get_cpu());

	x = 1;
	synchronize_rcu();
#ifdef ASSERT_0
	assert(0);
#endif
	y = 1;

	fake_release_cpu(get_cpu());
	return NULL;
}

void *run_gp_kthread(void *arg)
{
	struct rcu_state *rsp = arg;

	set_cpu(cpu0);
	current = rsp->gp_kthread; /* rcu_gp_kthread must not wake itself */
	
	fake_acquire_cpu(get_cpu());
	
	rcu_gp_kthread(rsp);
	
	fake_release_cpu(get_cpu());
	return NULL;
}

void *run_nocb_kthread(void *arg)
{
	set_cpu(cpu0);
	fake_acquire_cpu(get_cpu());

	rcu_nocb_kthread(arg);

	fake_release_cpu(get_cpu());
	return NULL;
}
	
int main()
{
	pthread_t tu;

	/* Initialize cpu_possible_mask, cpu_online_mask */
	set_online_cpus();
	set_possible_cpus();
	/* Initialize NOCB CPUs */
	rcu_nocb_setup("");
	/* Initialize leader stride so int_sqrt() is not called */
	rcu_nocb_leader_stride = 1;
	/* RCU initializations */
	rcu_init();
	rcu_init_nohz();
	/* All CPUs start out idle */
	for (int i = 0; i < NR_CPUS; i++) {
		set_cpu(i);
		rcu_idle_enter();
	}
	/* Spawn threads */
        rcu_spawn_gp_kthread();
        if (pthread_create(&tu, NULL, thread_update, NULL))
		abort();
	(void)thread_reader(NULL);

	if (pthread_join(tu, NULL))
		abort();
	
	BUG_ON(r_x == 0 && r_y == 1);
	
	return 0;
}
