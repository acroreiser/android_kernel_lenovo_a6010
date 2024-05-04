#define pr_fmt(fmt) "%s: " fmt "\n", __func__

#include <linux/kernel.h>
#include <linux/percpu-refcount.h>

/*
 * Initially, a percpu refcount is just a set of percpu counters. Initially, we
 * don't try to detect the ref hitting 0 - which means that get/put can just
 * increment or decrement the local counter. Note that the counter on a
 * particular cpu can (and will) wrap - this is fine, when we go to shutdown the
 * percpu counters will all sum to the correct value
 *
 * (More precisely: because moduler arithmatic is commutative the sum of all the
 * pcpu_count vars will be equal to what it would have been if all the gets and
 * puts were done to a single integer, even if some of the percpu integers
 * overflow or underflow).
 *
 * The real trick to implementing percpu refcounts is shutdown. We can't detect
 * the ref hitting 0 on every put - this would require global synchronization
 * and defeat the whole purpose of using percpu refs.
 *
 * What we do is require the user to keep track of the initial refcount; we know
 * the ref can't hit 0 before the user drops the initial ref, so as long as we
 * convert to non percpu mode before the initial ref is dropped everything
 * works.
 *
 * Converting to non percpu mode is done with some RCUish stuff in
 * percpu_ref_kill. Additionally, we need a bias value so that the atomic_t
 * can't hit 0 before we've added up all the percpu refs.
 */

#define PCPU_COUNT_BIAS		(1U << 31)

static unsigned __percpu *pcpu_count_ptr(struct percpu_ref *ref)
{
	return (unsigned __percpu *)(ref->pcpu_count_ptr & ~PCPU_REF_DEAD);
}

/**
 * percpu_ref_init - initialize a percpu refcount
 * @ref: percpu_ref to initialize
 * @release: function which will be called when refcount hits 0
 * @gfp: allocation mask to use
 *
 * Initializes the refcount in single atomic counter mode with a refcount of 1;
 * analagous to atomic_set(ref, 1).
 *
 * Note that @release must not sleep - it may potentially be called from RCU
 * callback context by percpu_ref_kill().
 */
int percpu_ref_init(struct percpu_ref *ref, percpu_ref_func_t *release,
		    gfp_t gfp)
{
	atomic_set(&ref->count, 1 + PCPU_COUNT_BIAS);

	ref->pcpu_count_ptr = (unsigned long)alloc_percpu_gfp(unsigned, gfp);
	if (!ref->pcpu_count_ptr)
		return -ENOMEM;

	ref->release = release;
	return 0;
}
EXPORT_SYMBOL(percpu_ref_init);

/**
 * percpu_ref_reinit - re-initialize a percpu refcount
 * @ref: perpcu_ref to re-initialize
 *
 * Re-initialize @ref so that it's in the same state as when it finished
 * percpu_ref_init().  @ref must have been initialized successfully, killed
 * and reached 0 but not exited.
 *
 * Note that percpu_ref_tryget[_live]() are safe to perform on @ref while
 * this function is in progress.
 */
void percpu_ref_reinit(struct percpu_ref *ref)
{
	unsigned __percpu *pcpu_count = pcpu_count_ptr(ref);
	int cpu;

	BUG_ON(!pcpu_count);
	WARN_ON(!percpu_ref_is_zero(ref));

	atomic_set(&ref->count, 1 + PCPU_COUNT_BIAS);

	/*
	 * Restore per-cpu operation.  smp_store_release() is paired with
	 * smp_read_barrier_depends() in __pcpu_ref_alive() and guarantees
	 * that the zeroing is visible to all percpu accesses which can see
	 * the following PCPU_REF_DEAD clearing.
	 */
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(pcpu_count, cpu) = 0;

	smp_store_release(&ref->pcpu_count_ptr,
			  ref->pcpu_count_ptr & ~PCPU_REF_DEAD);
}
EXPORT_SYMBOL_GPL(percpu_ref_reinit);

/**
 * percpu_ref_exit - undo percpu_ref_init()
 * @ref: percpu_ref to exit
 *
 * This function exits @ref.  The caller is responsible for ensuring that
 * @ref is no longer in active use.  The usual places to invoke this
 * function from are the @ref->release() callback or in init failure path
 * where percpu_ref_init() succeeded but other parts of the initialization
 * of the embedding object failed.
 */
void percpu_ref_exit(struct percpu_ref *ref)
{
	unsigned __percpu *pcpu_count = pcpu_count_ptr(ref);

	if (pcpu_count) {
		free_percpu(pcpu_count);
		ref->pcpu_count_ptr = PCPU_REF_DEAD;
	}
}
EXPORT_SYMBOL_GPL(percpu_ref_exit);

static void percpu_ref_kill_rcu(struct rcu_head *rcu)
{
	struct percpu_ref *ref = container_of(rcu, struct percpu_ref, rcu);
	unsigned __percpu *pcpu_count = pcpu_count_ptr(ref);
	unsigned count = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		count += *per_cpu_ptr(pcpu_count, cpu);

	pr_debug("global %i pcpu %i", atomic_read(&ref->count), (int) count);

	/*
	 * It's crucial that we sum the percpu counters _before_ adding the sum
	 * to &ref->count; since gets could be happening on one cpu while puts
	 * happen on another, adding a single cpu's count could cause
	 * @ref->count to hit 0 before we've got a consistent value - but the
	 * sum of all the counts will be consistent and correct.
	 *
	 * Subtracting the bias value then has to happen _after_ adding count to
	 * &ref->count; we need the bias value to prevent &ref->count from
	 * reaching 0 before we add the percpu counts. But doing it at the same
	 * time is equivalent and saves us atomic operations:
	 */

	atomic_add((int) count - PCPU_COUNT_BIAS, &ref->count);

	WARN_ONCE(atomic_read(&ref->count) <= 0, "percpu ref <= 0 (%i)",
		  atomic_read(&ref->count));

	/* @ref is viewed as dead on all CPUs, send out kill confirmation */
	if (ref->confirm_kill)
		ref->confirm_kill(ref);

	/*
	 * Now we're in single atomic_t mode with a consistent refcount, so it's
	 * safe to drop our initial ref:
	 */
	percpu_ref_put(ref);
}

/**
 * percpu_ref_kill_and_confirm - drop the initial ref and schedule confirmation
 * @ref: percpu_ref to kill
 * @confirm_kill: optional confirmation callback
 *
 * Equivalent to percpu_ref_kill() but also schedules kill confirmation if
 * @confirm_kill is not NULL.  @confirm_kill, which may not block, will be
 * called after @ref is seen as dead from all CPUs - all further
 * invocations of percpu_ref_tryget() will fail.  See percpu_ref_tryget()
 * for more details.
 *
 * Due to the way percpu_ref is implemented, @confirm_kill will be called
 * after at least one full RCU grace period has passed but this is an
 * implementation detail and callers must not depend on it.
 */
void percpu_ref_kill_and_confirm(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_kill)
{
	WARN_ONCE(ref->pcpu_count_ptr & PCPU_REF_DEAD,
		  "percpu_ref_kill() called more than once!\n");

	ref->pcpu_count_ptr |= PCPU_REF_DEAD;
	ref->confirm_kill = confirm_kill;

	call_rcu_sched(&ref->rcu, percpu_ref_kill_rcu);
}
EXPORT_SYMBOL(percpu_ref_kill_and_confirm);
