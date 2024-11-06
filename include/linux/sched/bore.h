
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/jump_label.h>

#ifndef _LINUX_SCHED_BORE_H
#define _LINUX_SCHED_BORE_H
#define SCHED_BORE_AUTHOR   "Masahito Suzuki"
#define SCHED_BORE_PROGNAME "BORE CPU Scheduler modification"
#define SCHED_BORE_VERSION  "6.6.3"

#ifdef CONFIG_SCHED_BORE
extern u8   __read_mostly sched_bore;
DECLARE_STATIC_KEY_TRUE(sched_bore_key);
extern u8   __read_mostly sched_burst_inherit_type;
extern u8   __read_mostly sched_burst_smoothness;
extern u8   __read_mostly sched_burst_penalty_offset;
extern uint __read_mostly sched_burst_penalty_scale;
extern uint __read_mostly sched_burst_cache_lifetime;

/* KMI-safe out-of-line storage for per-entity BORE state, see sched.h */
extern struct sched_bore_data init_task_bore_data;
extern int  alloc_task_bore(struct task_struct *p);
extern void free_task_bore(struct task_struct *p);

extern u8   effective_prio_bore(struct task_struct *p);
extern void update_curr_bore(struct task_struct *p, u64 delta_exec);
extern void restart_burst_bore(struct task_struct *p);
extern void restart_burst_rescale_deadline_bore(struct task_struct *p);
extern void task_fork_bore(
	struct task_struct *p, struct task_struct *parent, u64 clone_flags, u64 now);
extern void sched_init_bore(void);
extern void reset_task_bore(struct task_struct *p);

extern int sched_bore_update_handler(const struct ctl_table *table, int write,
	void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sched_burst_inherit_type_update_handler(const struct ctl_table *table,
	int write, void __user *buffer, size_t *lenp, loff_t *ppos);

extern void reweight_entity(
	struct cfs_rq *cfs_rq, struct sched_entity *se, unsigned long weight);
#endif // CONFIG_SCHED_BORE
#endif // _LINUX_SCHED_BORE_H
