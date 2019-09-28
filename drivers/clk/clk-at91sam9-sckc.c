/*
 * clk-at91sam9-sckc.c
 *
 *  Created on: Jul 9, 2017
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/delay.h>

#define SLOW_CLOCK_FREQ		32768
#define SLOWCK_SW_CYCLES	5
#define SLOWCK_SW_TIME_USEC	((SLOWCK_SW_CYCLES * USEC_PER_SEC) / \
				 SLOW_CLOCK_FREQ)

#define SCKC_CR       0x0

#define SCKC_CR_OSCSEL_SHIFT    3
#define SCKC_CR_OSC32BYP_SHIFT  2
#define SCKC_CR_OSC32EN_SHIFT   1
#define SCKC_CR_RCEN_SHIFT      0

#define SCKC_CR_OSCSEL_MASK     1
#define SCKC_CR_OSCSEL_XTAL     1
#define SCKC_CR_OSCSEL_RC       0

#define SCKC_CR_OSC32EN_MASK    1
#define SCKC_CR_OSC32EN_ENABLE  1
#define SCKC_CR_OSC32EN_DISABLE 0

#define SCKC_CR_RCEN_MASK       1
#define SCKC_CR_RCEN_ENABLE     1
#define SCKC_CR_RCEN_DISABLE    0

#define SCKC_CR_OSC32BYP_MASK    1
#define SCKC_CR_OSC32BYP_ENABLE  1
#define SCKC_CR_OSC32BYP_DISABLE 0

#define __FIELD_MASK(regname,fieldname) \
    (regname##_##fieldname##_MASK<<regname##_##fieldname##_SHIFT)

#define __FIELD_VALUE_FIXED(regname,fieldname,valname) \
    (regname##_##fieldname##_##valname<<regname##_##fieldname##_SHIFT)

#define __FIELD_IS_SET(regname,fieldname,valname,value) \
    (((value) &(regname##_##fieldname##_MASK<<regname##_##fieldname##_SHIFT)) == \
    (regname##_##fieldname##_##valname<<regname##_##fieldname##_SHIFT))

#define __FIELD_VALUE_SET(regname,fieldname,value) \
    (((value)&regname##_##fieldname##_MASK)<<regname##_##fieldname##_SHIFT)

#define __FIELD_VALUE_GET(regname,fieldname,value) \
    (((value)>>regname##_##fieldname##_SHIFT)&regname##_##fieldname##_MASK)


struct sckc_clk_slow_rc_osc{
	struct clk_hw hw;
	void * __iomem base;
	uint32_t startup_usec;
	uint32_t frequency;
	uint32_t accuracy;
};
#define to_sckc_clk_slow_rc_osc(hw) container_of(hw, struct sckc_clk_slow_rc_osc, hw)

struct sckc_clk_slow_osc{
	struct clk_hw hw;
	void * __iomem base;
	uint32_t startup_usec;
};
#define to_sckc_clk_slow_osc(hw) container_of(hw, struct sckc_clk_slow_osc, hw)

struct sckc_clk_slow{
	struct clk_hw hw;
	void * __iomem base;
};
#define to_sckc_clk_slow(hw) container_of(hw, struct sckc_clk_slow, hw)

static void * __iomem sckc_base;

static int sckc_clk_slow_rc_osc_prepare(struct clk_hw *hw)
{
	struct sckc_clk_slow_rc_osc *clk_data = to_sckc_clk_slow_rc_osc(hw);
	uint32_t value;

	value =  readl_relaxed(clk_data->base + SCKC_CR) ;
	if(__FIELD_IS_SET(SCKC_CR,RCEN,ENABLE,value)){
		return 0;
	}

	value &= ~__FIELD_MASK(SCKC_CR,RCEN);
	value |= __FIELD_VALUE_FIXED(SCKC_CR,RCEN,ENABLE);

	writel_relaxed(value,clk_data->base + SCKC_CR);

	usleep_range(clk_data->startup_usec, clk_data->startup_usec + 1);

	return 0;

}
static void sckc_clk_slow_rc_osc_unprepare(struct clk_hw *hw)
{
	struct sckc_clk_slow_rc_osc *clk_data = to_sckc_clk_slow_rc_osc(hw);
	uint32_t value;

	value =  readl_relaxed(clk_data->base + SCKC_CR) ;
	if(__FIELD_IS_SET(SCKC_CR,RCEN,DISABLE,value)){
		return ;
	}

	value &= ~__FIELD_MASK(SCKC_CR,RCEN);
	value |= __FIELD_VALUE_FIXED(SCKC_CR,RCEN,DISABLE);

	writel_relaxed(value,clk_data->base + SCKC_CR);

}
static int sckc_clk_slow_rc_osc_is_prepared(struct clk_hw *hw)
{
	struct sckc_clk_slow_rc_osc *clk_data = to_sckc_clk_slow_rc_osc(hw);
	uint32_t value;

	value =  readl_relaxed(clk_data->base + SCKC_CR) ;

	return __FIELD_IS_SET(SCKC_CR,RCEN,ENABLE,value);

}

static unsigned long sckc_clk_slow_rc_osc_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct sckc_clk_slow_rc_osc *clk_data = to_sckc_clk_slow_rc_osc(hw);

	return clk_data->frequency;
}


static unsigned long sckc_clk_slow_rc_osc_recalc_accuracy(struct clk_hw *hw,
						     unsigned long parent_acc)
{
	struct sckc_clk_slow_rc_osc *clk_data = to_sckc_clk_slow_rc_osc(hw);

	return clk_data->accuracy;
}



static const struct clk_ops sckc_clk_slow_rc_osc_ops = {
	.prepare = sckc_clk_slow_rc_osc_prepare,
	.unprepare = sckc_clk_slow_rc_osc_unprepare,
	.is_prepared = sckc_clk_slow_rc_osc_is_prepared,
	.recalc_rate = sckc_clk_slow_rc_osc_recalc_rate,
	.recalc_accuracy = sckc_clk_slow_rc_osc_recalc_accuracy,
};

static void __init sckc_clk_slow_rc_osc_init(struct device_node *np)
{
	struct clk_init_data init;
	struct sckc_clk_slow_rc_osc *clk_data;
	const char *parent_names[2];
	const char *name = np->name;
	uint32_t frequency,accuracy,startup;
	int num_parents;
	int ret;

	if(!sckc_base){
		return;
	}

	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(clk_data){
		return ;
	}

	clk_data->base = sckc_base;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents != 2){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np,"clock-output-names",&name);
	of_property_read_u32(np,"clock-frequency",&frequency);
	of_property_read_u32(np,"clock-accuracy",&accuracy);
	of_property_read_u32(np, "atmel,startup-time-usec", &startup);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_IGNORE_UNUSED;
	init.ops = &sckc_clk_slow_rc_osc_ops;


	clk_data->hw.init = &init;
	clk_data->frequency = frequency;
	clk_data->accuracy = accuracy;
	clk_data->startup_usec = startup;
	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);


}
CLK_OF_DECLARE(at91sam9_sckc_clk_slow_rc_osc,"atmel,at91sam9-clk-slow-rc-osc",sckc_clk_slow_rc_osc_init);

static int sckc_clk_slow_osc_prepare(struct clk_hw *hw)
{
	struct sckc_clk_slow_osc *clk_data = to_sckc_clk_slow_osc(hw);
	uint32_t value;

	value =  readl_relaxed(clk_data->base + SCKC_CR) ;
	if(__FIELD_IS_SET(SCKC_CR,OSC32BYP,ENABLE,value)||
			__FIELD_IS_SET(SCKC_CR,OSC32EN,ENABLE,value)){
		return 0;
	}

	value &= ~__FIELD_MASK(SCKC_CR,OSC32EN);
	value |= __FIELD_VALUE_FIXED(SCKC_CR,OSC32EN,ENABLE);

	writel_relaxed(value,clk_data->base + SCKC_CR);

	usleep_range(clk_data->startup_usec, clk_data->startup_usec + 1);

	return 0;
}
static void sckc_clk_slow_osc_unprepare(struct clk_hw *hw)
{
	struct sckc_clk_slow_osc *clk_data = to_sckc_clk_slow_osc(hw);
	uint32_t value;

	value =  readl_relaxed(clk_data->base + SCKC_CR) ;
	if(__FIELD_IS_SET(SCKC_CR,OSC32BYP,ENABLE,value)||
			__FIELD_IS_SET(SCKC_CR,OSC32EN,DISABLE,value)){
		return ;
	}

	value &= ~__FIELD_MASK(SCKC_CR,OSC32EN);
	value |= __FIELD_VALUE_FIXED(SCKC_CR,OSC32EN,DISABLE);

	writel_relaxed(value,clk_data->base + SCKC_CR);
}
static int sckc_clk_slow_osc_is_prepared(struct clk_hw *hw)
{
	struct sckc_clk_slow_osc *clk_data = to_sckc_clk_slow_osc(hw);
	uint32_t value;

	value =  readl_relaxed(clk_data->base + SCKC_CR) ;

	return __FIELD_IS_SET(SCKC_CR,OSC32EN,ENABLE,value);

}

static const struct clk_ops sckc_clk_slow_osc_ops = {
	.prepare = sckc_clk_slow_osc_prepare,
	.unprepare = sckc_clk_slow_osc_unprepare,
	.is_prepared = sckc_clk_slow_osc_is_prepared,
};

static void __init sckc_clk_slow_osc_init(struct device_node *np)
{
	struct clk_init_data init;
	struct sckc_clk_slow_osc *clk_data;
	const char *parent_names[2];
	const char *name = np->name;
	int num_parents;
	int ret;
	uint32_t startup;

	if(!sckc_base){
		return;
	}

	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(clk_data){
		return ;
	}

	clk_data->base = sckc_base;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents != 2){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np,"clock-output-names",&name);
	of_property_read_u32(np, "atmel,startup-time-usec", &startup);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_IGNORE_UNUSED;
	init.ops = &sckc_clk_slow_osc_ops;


	clk_data->hw.init = &init;
	clk_data->startup_usec = startup;
	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);
}
CLK_OF_DECLARE(at91sam9_sckc_clk_slow_osc,"atmel,at91sam9-clk-slow-osc",sckc_clk_slow_osc_init);

static uint8_t clk_slow_get_parent(struct clk_hw *hw)
{
	struct sckc_clk_slow *clk_data = to_sckc_clk_slow(hw);
	uint32_t value;

	value = readl_relaxed(clk_data->base + SCKC_CR);

	return __FIELD_VALUE_GET(SCKC_CR,OSCSEL,value);
}

static int clk_slow_set_parent(struct clk_hw *hw, uint8_t index)
{
	struct sckc_clk_slow *clk_data = to_sckc_clk_slow(hw);
	uint32_t value;

	if(index > 1){
		return -EINVAL;
	}

	value = readl_relaxed(clk_data->base + SCKC_CR);

	if(__FIELD_VALUE_GET(SCKC_CR,OSCSEL,value) == index){
		return 0;
	}

	value &= ~__FIELD_MASK(SCKC_CR,OSCSEL);
	value |= __FIELD_VALUE_SET(SCKC_CR,OSCSEL,index);

	writel_relaxed(value,clk_data->base + SCKC_CR);

	usleep_range(SLOWCK_SW_TIME_USEC, SLOWCK_SW_TIME_USEC + 1);

	return 0;
}

static const struct clk_ops sckc_clk_slow_ops = {
	.get_parent = clk_slow_get_parent,
	.set_parent = clk_slow_set_parent,
};

static void __init sckc_clk_slow_init(struct device_node *np)
{
	struct clk_init_data init;
	struct sckc_clk_slow *clk_data;
	const char *parent_names[2];
	const char *name = np->name;
	int num_parents;
	int ret;

	if(!sckc_base){
		return;
	}

	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(clk_data){
		return ;
	}

	clk_data->base = sckc_base;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents != 2){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &sckc_clk_slow_ops;


	clk_data->hw.init = &init;
	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);
}
CLK_OF_DECLARE(at91sam9_sckc_clk_slow,"atmel,at91sam9-clk-slow",sckc_clk_slow_init);


static void __init sckc_clk_init(struct device_node *np)
{
	sckc_base = of_iomap(np,0);
	if(!sckc_base){
		pr_info("failed to iomap sckc base\n");
		return;
	}
}
CLK_OF_DECLARE(at91sam9_sckc,"atmel,at91sam9-sckc",sckc_clk_init);


