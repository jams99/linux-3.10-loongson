#include <linux/init.h>
#include <linux/pci.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <hpet.h>
#include <asm/time.h>

#define HPET_MIN_CYCLES		16
#define HPET_MIN_PROG_DELTA	(HPET_MIN_CYCLES * 12)

int hpet_enabled;
unsigned int hpet_t0_cfg;
static DEFINE_SPINLOCK(hpet_lock);
DEFINE_PER_CPU(struct clock_event_device, hpet_clockevent_device);

static int hpet_read(int offset)
{
	return readl((void *)(HPET_MMIO_ADDR + offset));
}

static void hpet_write(int offset, int data)
{
	writel(data,(void *)(HPET_MMIO_ADDR + offset));
}

static void hpet_start_counter(void)
{
	unsigned int cfg = hpet_read(HPET_CFG);
	cfg |= HPET_CFG_ENABLE;
	hpet_write(HPET_CFG, cfg);
}

static void hpet_stop_counter(void)
{
	unsigned int cfg = hpet_read(HPET_CFG);

	cfg &= ~HPET_CFG_ENABLE;
	hpet_write(HPET_CFG, cfg);
}

static void hpet_reset_counter(void)
{
	hpet_write(HPET_COUNTER, 0);
	hpet_write(HPET_COUNTER + 4, 0);
}

static void hpet_restart_counter(void)
{
	hpet_stop_counter();
	hpet_reset_counter();
	hpet_start_counter();
}

static void hpet_enable_legacy_int(void)
{
	unsigned int cfg = hpet_read(HPET_CFG);

	cfg |= HPET_CFG_LEGACY;
}
void clean_comparator_value(void)
{
	int cfg;
	cfg = hpet_read(HPET_T0_CMP);
	cfg &= 0;
	hpet_write(HPET_T0_CMP,cfg);
}
void clean_timer_config(void)
{
	int cfg;
	cfg = hpet_read(HPET_T0_CFG);
	cfg &= 0;
	hpet_write(HPET_T0_CFG,cfg);
}

void soft_reset(void)
{
	hpet_stop_counter();
	hpet_reset_counter();
	clean_timer_config();
	clean_comparator_value();
}
static void hpet_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	int cfg = 0;
	spin_lock(&hpet_lock);
	switch (mode) {
		case CLOCK_EVT_MODE_PERIODIC:
			printk(KERN_INFO "set clock event to periodic\n");
			soft_reset();
			cfg = hpet_read(HPET_T0_CFG);
			cfg |= 0x48;
			hpet_write(HPET_T0_CFG,cfg);
			hpet_write(HPET_T0_CMP,HPET_COMPARE_VAL);
			cfg = hpet_read(HPET_T0_CFG);
			cfg |= 0xe;
			hpet_write(HPET_T0_CFG,cfg);
			hpet_start_counter();
			break;
		case CLOCK_EVT_MODE_SHUTDOWN:
		case CLOCK_EVT_MODE_UNUSED:
			cfg = hpet_read(HPET_T0_CFG);
			cfg &= ~HPET_TN_ENABLE;
			hpet_write(HPET_T0_CFG, cfg);
			break;
		case CLOCK_EVT_MODE_ONESHOT:
			printk(KERN_INFO "set clock event to one shot mode!\n");
			cfg = hpet_read(HPET_T0_CFG);
			/* set timer0 type
			 * 1 : periodic interrupt
			 * 0 : non-periodic(oneshot) interrupt
			 */
			cfg &= ~HPET_TN_PERIODIC;
			cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
			hpet_write(HPET_T0_CFG, cfg);
			break;
		case CLOCK_EVT_MODE_RESUME:
			hpet_enable_legacy_int();
			break;
	}
	spin_unlock(&hpet_lock);
}

static int hpet_next_event(unsigned long delta,
		struct clock_event_device *evt)
{
	unsigned int cnt;
	int res;

	cnt = hpet_read(HPET_COUNTER);
	cnt += delta;
	hpet_write(HPET_T0_CMP, cnt);

	res = ((int)(hpet_read(HPET_COUNTER) - cnt) > 0) ? -ETIME : 0;
	return res;
}

static irqreturn_t hpet_irq_handler(int irq, void *data)
{
	int is_irq;
	struct clock_event_device *cd;
	unsigned int cpu = smp_processor_id();
	is_irq = hpet_read(HPET_STATUS);
	if (is_irq & HPET_T0_IRS) {
		/* clear the TIMER0 irq status register */
		hpet_write(HPET_STATUS, HPET_T0_IRS);
		cd = &per_cpu(hpet_clockevent_device, cpu);
		cd->event_handler(cd);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static struct irqaction hpet_irq = {
	.handler = hpet_irq_handler,
	.flags = IRQF_NOBALANCING | IRQF_TIMER,
	.name = "hpet",
};
void __init setup_hpet_timer(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;
	hpet_enabled = 1;
	cd = &per_cpu(hpet_clockevent_device, cpu);
	cd->name = "hpet";
	cd->rating = 300;
	cd->features =   CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	cd->set_mode = hpet_set_mode;
	cd->set_next_event = hpet_next_event;
	cd->irq = HPET_T0_IRQ;
	cd->cpumask = cpumask_of(cpu);
	clockevent_set_clock(cd, HPET_FREQ);
	cd->max_delta_ns = clockevent_delta2ns(0x7fffffff, cd);
	cd->min_delta_ns = clockevent_delta2ns(HPET_MIN_PROG_DELTA, cd);

	clockevents_register_device(cd);
	setup_irq(HPET_T0_IRQ, &hpet_irq);
	printk(KERN_INFO "hpet clock event device register\n");
}

static cycle_t hpet_read_counter(struct clocksource *cs)
{
	return (cycle_t)hpet_read(HPET_COUNTER);
}

static void hpet_suspend(struct clocksource *cs)
{
	hpet_t0_cfg = hpet_read(HPET_T0_CFG);
}

static void hpet_resume(struct clocksource *cs)
{
	hpet_write(HPET_T0_CFG, hpet_t0_cfg);
	hpet_restart_counter();
}

static struct clocksource csrc_hpet = {
	.name = "hpet",
	/* mips clocksource rating is less than 300, so hpet is batter. */
	.rating = 300,
	.read = hpet_read_counter,
	.mask = CLOCKSOURCE_MASK(32),
	/* oneshot mode work normal with this flag */
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.suspend = hpet_suspend,
	.resume = hpet_resume,
	.mult = 0,
	.shift = 10,
};

int __init init_hpet_clocksource(void)
{
	if(!hpet_enabled)
		return -EBUSY;
	csrc_hpet.mult = clocksource_hz2mult(HPET_FREQ, csrc_hpet.shift);
	return clocksource_register_hz(&csrc_hpet, HPET_FREQ);
}

arch_initcall(init_hpet_clocksource);
