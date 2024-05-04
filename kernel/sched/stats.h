
#ifdef CONFIG_SCHEDSTATS

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{
	if (rq) {
		rq->rq_sched_info.run_delay += delta;
		rq->rq_sched_info.pcount++;
	}
}

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_cpu_time += delta;
}

static inline void
rq_sched_info_dequeued(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_sched_info.run_delay += delta;
}
# define schedstat_inc(rq, field)	do { (rq)->field++; } while (0)
# define schedstat_add(rq, field, amt)	do { (rq)->field += (amt); } while (0)
# define schedstat_set(var, val)	do { var = (val); } while (0)
#else /* !CONFIG_SCHEDSTATS */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{}
static inline void
rq_sched_info_dequeued(struct rq *rq, unsigned long long delta)
{}
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{}
# define schedstat_inc(rq, field)	do { } while (0)
# define schedstat_add(rq, field, amt)	do { } while (0)
# define schedstat_set(var, val)	do { } while (0)
#endif

#ifdef CONFIG_PSI
/*
 * PSI tracks state that persists across sleeps, such as iowaits and
 * memory stalls. As a result, it has to distinguish between sleeps,
 * where a task's runnable state changes, and requeues, where a task
 * and its state are being moved between CPUs and runqueues.
 */
static inline void psi_enqueue(struct task_struct *p, bool wakeup)
{
	int clear = 0, set = TSK_RUNNING;

	if (psi_disabled)
		return;

	if (!wakeup || p->sched_psi_wake_requeue) {
		if (p->flags & PF_MEMSTALL)
			set |= TSK_MEMSTALL;
		if (p->sched_psi_wake_requeue)
			p->sched_psi_wake_requeue = 0;
	} else {
		if (p->in_iowait)
			clear |= TSK_IOWAIT;
	}

	psi_task_change(p, clear, set);
}

static inline void psi_dequeue(struct task_struct *p, bool sleep)
{
	int clear = TSK_RUNNING, set = 0;

	if (psi_disabled)
		return;

	if (!sleep) {
		if (p->flags & PF_MEMSTALL)
			clear |= TSK_MEMSTALL;
	} else {
		if (p->in_iowait)
			set |= TSK_IOWAIT;
	}

	psi_task_change(p, clear, set);
}

static inline void psi_ttwu_dequeue(struct task_struct *p)
{
	if (psi_disabled)
		return;
	/*
	 * Is the task being migrated during a wakeup? Make sure to
	 * deregister its sleep-persistent psi states from the old
	 * queue, and let psi_enqueue() know it has to requeue.
	 */
	if (unlikely(p->in_iowait || (p->flags & PF_MEMSTALL))) {
		struct rq *rq;
		int clear = 0;

		if (p->in_iowait)
			clear |= TSK_IOWAIT;
		if (p->flags & PF_MEMSTALL)
			clear |= TSK_MEMSTALL;

		rq = __task_rq_lock(p);
		psi_task_change(p, clear, 0);
		p->sched_psi_wake_requeue = 1;
		__task_rq_unlock(rq);
	}
}

static inline void psi_task_tick(struct rq *rq)
{
	if (psi_disabled)
		return;

	if (unlikely(rq->curr->flags & PF_MEMSTALL))
		psi_memstall_tick(rq->curr, cpu_of(rq));
}
#else /* CONFIG_PSI */
static inline void psi_enqueue(struct task_struct *p, bool wakeup) {}
static inline void psi_dequeue(struct task_struct *p, bool sleep) {}
static inline void psi_ttwu_dequeue(struct task_struct *p) {}
static inline void psi_task_tick(struct rq *rq) {}
#endif /* CONFIG_PSI */

#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
static inline void sched_info_reset_dequeued(struct task_struct *t)
{
	t->sched_info.last_queued = 0;
}

/*
 * We are interested in knowing how long it was from the *first* time a
 * task was queued to the time that it finally hit a cpu, we call this routine
 * from dequeue_task() to account for possible rq->clock skew across cpus. The
 * delta taken on each cpu would annul the skew.
 */
static inline void sched_info_dequeued(struct task_struct *t)
{
	unsigned long long now = task_rq(t)->clock, delta = 0;

	if (unlikely(sched_info_on()))
		if (t->sched_info.last_queued)
			delta = now - t->sched_info.last_queued;
	sched_info_reset_dequeued(t);
	t->sched_info.run_delay += delta;

	rq_sched_info_dequeued(task_rq(t), delta);
}

/*
 * Called when a task finally hits the cpu.  We can now calculate how
 * long it was waiting to run.  We also note when it began so that we
 * can keep stats on how long its timeslice is.
 */
static void sched_info_arrive(struct task_struct *t)
{
	unsigned long long now = task_rq(t)->clock, delta = 0;

	if (t->sched_info.last_queued)
		delta = now - t->sched_info.last_queued;
	sched_info_reset_dequeued(t);
	t->sched_info.run_delay += delta;
	t->sched_info.last_arrival = now;
	t->sched_info.pcount++;

	rq_sched_info_arrive(task_rq(t), delta);
}

/*
 * This function is only called from enqueue_task(), but also only updates
 * the timestamp if it is already not set.  It's assumed that
 * sched_info_dequeued() will clear that stamp when appropriate.
 */
static inline void sched_info_queued(struct task_struct *t)
{
	if (unlikely(sched_info_on()))
		if (!t->sched_info.last_queued)
			t->sched_info.last_queued = task_rq(t)->clock;
}

/*
 * Called when a process ceases being the active-running process, either
 * voluntarily or involuntarily.  Now we can calculate how long we ran.
 * Also, if the process is still in the TASK_RUNNING state, call
 * sched_info_queued() to mark that it has now again started waiting on
 * the runqueue.
 */
static inline void sched_info_depart(struct task_struct *t)
{
	unsigned long long delta = task_rq(t)->clock -
					t->sched_info.last_arrival;

	rq_sched_info_depart(task_rq(t), delta);

	if (t->state == TASK_RUNNING)
		sched_info_queued(t);
}

/*
 * Called when tasks are switched involuntarily due, typically, to expiring
 * their time slice.  (This may also be called when switching to or from
 * the idle task.)  We are only called when prev != next.
 */
static inline void
__sched_info_switch(struct task_struct *prev, struct task_struct *next)
{
	struct rq *rq = task_rq(prev);

	/*
	 * prev now departs the cpu.  It's not interesting to record
	 * stats about how efficient we were at scheduling the idle
	 * process, however.
	 */
	if (prev != rq->idle)
		sched_info_depart(prev);

	if (next != rq->idle)
		sched_info_arrive(next);
}
static inline void
sched_info_switch(struct task_struct *prev, struct task_struct *next)
{
	if (unlikely(sched_info_on()))
		__sched_info_switch(prev, next);
}
#else
#define sched_info_queued(t)			do { } while (0)
#define sched_info_reset_dequeued(t)	do { } while (0)
#define sched_info_dequeued(t)			do { } while (0)
#define sched_info_switch(t, next)		do { } while (0)
#endif /* CONFIG_SCHEDSTATS || CONFIG_TASK_DELAY_ACCT */

/*
 * The following are functions that support scheduler-internal time accounting.
 * These functions are generally called at the timer tick.  None of this depends
 * on CONFIG_SCHEDSTATS.
 */

/**
 * cputimer_running - return true if cputimer is running
 *
 * @tsk:	Pointer to target task.
 */
static inline bool cputimer_running(struct task_struct *tsk)

{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;

	if (!cputimer->running)
		return false;

	/*
	 * After we flush the task's sum_exec_runtime to sig->sum_sched_runtime
	 * in __exit_signal(), we won't account to the signal struct further
	 * cputime consumed by that task, even though the task can still be
	 * ticking after __exit_signal().
	 *
	 * In order to keep a consistent behaviour between thread group cputime
	 * and thread group cputimer accounting, lets also ignore the cputime
	 * elapsing after __exit_signal() in any thread group timer running.
	 *
	 * This makes sure that POSIX CPU clocks and timers are synchronized, so
	 * that a POSIX CPU timer won't expire while the corresponding POSIX CPU
	 * clock delta is behind the expiring timer value.
	 */
	if (unlikely(!tsk->sighand))
		return false;

	return true;
}

/**
 * account_group_user_time - Maintain utime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @cputime:	Time value by which to increment the utime field of the
 *		thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the utime field there.
 */
static inline void account_group_user_time(struct task_struct *tsk,
					   cputime_t cputime)
{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;

	if (!cputimer_running(tsk))
		return;

	raw_spin_lock(&cputimer->lock);
	cputimer->cputime.utime += cputime;
	raw_spin_unlock(&cputimer->lock);
}

/**
 * account_group_system_time - Maintain stime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @cputime:	Time value by which to increment the stime field of the
 *		thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the stime field there.
 */
static inline void account_group_system_time(struct task_struct *tsk,
					     cputime_t cputime)
{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;

	if (!cputimer_running(tsk))
		return;

	raw_spin_lock(&cputimer->lock);
	cputimer->cputime.stime += cputime;
	raw_spin_unlock(&cputimer->lock);
}

/**
 * account_group_exec_runtime - Maintain exec runtime for a thread group.
 *
 * @tsk:	Pointer to task structure.
 * @ns:		Time value by which to increment the sum_exec_runtime field
 *		of the thread_group_cputime structure.
 *
 * If thread group time is being maintained, get the structure for the
 * running CPU and update the sum_exec_runtime field there.
 */
static inline void account_group_exec_runtime(struct task_struct *tsk,
					      unsigned long long ns)
{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;

	if (!cputimer_running(tsk))
		return;

	raw_spin_lock(&cputimer->lock);
	cputimer->cputime.sum_exec_runtime += ns;
	raw_spin_unlock(&cputimer->lock);
}
