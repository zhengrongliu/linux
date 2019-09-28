/*
 * clk-am335x-prcm.c
 *
 *  Created on: Feb 11, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/rational.h>
#include <linux/mfd/syscon.h>

#define CM_PER_BASE		0x00
#define CM_WKUP_BASE		0x400
#define CM_DPLL_BASE		0x500
#define CM_MPU_BASE		0x600
#define CM_DEVICE_BASE		0x700
#define CM_RTC_BASE		0x800
#define CM_GFX_BASE		0x900
#define CM_CEFUSE_BASE		0xa00

#define CM_PER_OFFSET(x)	(CM_PER_BASE + (x))
#define CM_WKUP_OFFSET(x) 	(CM_WKUP_BASE + (x))

/*
 * CM_PER registers
 */
#define CM_PER_L4LS_CLKSTCTRL		CM_PER_OFFSET(0x0)
#define CM_PER_L3S_CLKSTCTRL		CM_PER_OFFSET(0x4)
#define CM_PER_L3_CLKSTCTRL		CM_PER_OFFSET(0xc)

/*
 * CM_WKUP registers
 */
#define CM_CLKSEL_DPLL_CORE		CM_WKUP_OFFSET(0x68)
#define CM_IDLEST_DPLL_PER		CM_WKUP_OFFSET(0x70)
#define CM_DIV_M4_DPLL_CORE		CM_WKUP_OFFSET(0x80)
#define CM_DIV_M5_DPLL_CORE		CM_WKUP_OFFSET(0x84)
#define CM_CLKMODE_DPLL_MPU		CM_WKUP_OFFSET(0x88)
#define CM_CLKMODE_DPLL_PER		CM_WKUP_OFFSET(0x8c)
#define CM_CLKMODE_DPLL_CORE		CM_WKUP_OFFSET(0x90)
#define CM_CLKMODE_DPLL_DDR		CM_WKUP_OFFSET(0x94)
#define CM_CLKMODE_DPLL_DISP		CM_WKUP_OFFSET(0x98)
#define CM_CLKSEL_DPLL_PERIPH		CM_WKUP_OFFSET(0x9c)
#define CM_DIV_M2_DPLL_DDR		CM_WKUP_OFFSET(0xa0)
#define CM_DIV_M2_DPLL_DISP		CM_WKUP_OFFSET(0xa4)
#define CM_DIV_M2_DPLL_MPU		CM_WKUP_OFFSET(0xa8)
#define CM_DIV_M2_DPLL_PER		CM_WKUP_OFFSET(0xac)
#define CM_DIV_M6_DPLL_CORE		CM_WKUP_OFFSET(0xb8)

/*
 * CM_CLKMODE_DPLL_PER register
 */
#define CM_CLKMODE_DPLL_PER_DPLL_EN_SHIFT		0
#define CM_CLKMODE_DPLL_PER_DPLL_EN_WIDTH		3
#define CM_CLKMODE_DPLL_PER_DPLL_EN_STOP		1
#define CM_CLKMODE_DPLL_PER_DPLL_EN_BYPASS		4
#define CM_CLKMODE_DPLL_PER_DPLL_EN_BYPASS_LOW		5
#define CM_CLKMODE_DPLL_PER_DPLL_EN_ENABLE		7
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_EN_SHIFT		12
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_EN_WIDTH		1
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_ACK_SHIFT		13
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_ACK_WIDTH		1
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_DOWNSPREAD_SHIFT	14
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_DOWNSPREAD_WIDTH	1
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_TYPE_SHIFT		15
#define CM_CLKMODE_DPLL_PER_DPLL_SSC_TYPE_WIDTH		1


/*
 * CM_CLKSEL_DPLL_CORE register
 */
#define CM_CLKSEL_DPLL_CORE_DPLL_DIV_SHIFT	0
#define CM_CLKSEL_DPLL_CORE_DPLL_DIV_WIDTH	7
#define CM_CLKSEL_DPLL_CORE_DPLL_MULT_SHIFT	8
#define CM_CLKSEL_DPLL_CORE_DPLL_MULT_WIDTH	11
/*
 * CM_CLKSEL_DPLL_PERIPH register
 */
#define CM_CLKSEL_DPLL_PERIPH_DPLL_DIV_SHIFT 	0
#define CM_CLKSEL_DPLL_PERIPH_DPLL_DIV_WIDTH 	8
#define CM_CLKSEL_DPLL_PERIPH_DPLL_MULT_SHIFT 	8
#define CM_CLKSEL_DPLL_PERIPH_DPLL_MULT_WIDTH 	12

/*
 * CM_IDLEST_DPLL_PER register
 */
#define CM_IDLEST_DPLL_PER_ST_DPLL_CLK_SHIFT	0
#define CM_IDLEST_DPLL_PER_ST_DPLL_CLK_WIDTH	1
#define CM_IDLEST_DPLL_PER_ST_DPLL_CLK_LOCKED	1
#define CM_IDLEST_DPLL_PER_ST_DPLL_CLK_UNLOCKED	0

#define CM_IDLEST_DPLL_PER_ST_MN_BYPASS_SHIFT	8
#define CM_IDLEST_DPLL_PER_ST_MN_BYPASS_WIDTH	1
#define CM_IDLEST_DPLL_PER_ST_MN_BYPASS_TRUE	1
#define CM_IDLEST_DPLL_PER_ST_MN_BYPASS_FALSE	0

/*
 * CM_DIV_M2_DPLL_PER register
 */
#define CM_DIV_M2_DPLL_PER_DIV_SHIFT		0
#define CM_DIV_M2_DPLL_PER_DIV_WIDTH		7
#define CM_DIV_M2_DPLL_PER_DIVCHACK_SHIFT	7
#define CM_DIV_M2_DPLL_PER_DIVCHACK_WIDTH	1
#define CM_DIV_M2_DPLL_PER_DIVCHACK_TRUE	1
#define CM_DIV_M2_DPLL_PER_GATE_SHIFT		8
#define CM_DIV_M2_DPLL_PER_GATE_WIDTH		1
#define CM_DIV_M2_DPLL_PER_GATE_ALWAYS		1
#define CM_DIV_M2_DPLL_PER_GATE_AUTO		0
#define CM_DIV_M2_DPLL_PER_STATUS_SHIFT		9
#define CM_DIV_M2_DPLL_PER_STATUS_WIDTH		1
#define CM_DIV_M2_DPLL_PER_STATUS_DISABLED	0
#define CM_DIV_M2_DPLL_PER_STATUS_ENABLED	1

/*
 * CM_DIV_Mx_DPLL_CORE register
 */
#define CM_DIV_Mx_DPLL_CORE_DIV_SHIFT		0
#define CM_DIV_Mx_DPLL_CORE_DIV_WIDTH		5
#define CM_DIV_Mx_DPLL_CORE_DIVCHACK_SHIFT	5
#define CM_DIV_Mx_DPLL_CORE_DIVCHACK_WIDTH	1
#define CM_DIV_Mx_DPLL_CORE_DIVCHACK_TRUE	1
#define CM_DIV_Mx_DPLL_CORE_GATE_SHIFT		8
#define CM_DIV_Mx_DPLL_CORE_GATE_WIDTH		1
#define CM_DIV_Mx_DPLL_CORE_GATE_ALWAYS		1
#define CM_DIV_Mx_DPLL_CORE_GATE_AUTO		0
#define CM_DIV_Mx_DPLL_CORE_STATUS_SHIFT	9
#define CM_DIV_Mx_DPLL_CORE_STATUS_WIDTH	1
#define CM_DIV_Mx_DPLL_CORE_STATUS_DISABLED	0
#define CM_DIV_Mx_DPLL_CORE_STATUS_ENABLED	1
#define CM_DIV_Mx_DPLL_CORE_POWER_SHIFT		12
#define CM_DIV_Mx_DPLL_CORE_POWER_WIDTH		1
#define CM_DIV_Mx_DPLL_CORE_POWER_KEEP		0
#define CM_DIV_Mx_DPLL_CORE_POWER_AUTO		1

/*
 * WKUP_CLKCTRL register
 */
#define CM_WKUP_CLKCTRL_MODULE_MODE_SHIFT		0
#define CM_WKUP_CLKCTRL_MODULE_MODE_WIDTH		2
#define CM_WKUP_CLKCTRL_MODULE_MODE_DISABLE		0
#define CM_WKUP_CLKCTRL_MODULE_MODE_ENABLE		2
#define CM_WKUP_CLKCTRL_MODULE_IDLEST_SHIFT		16
#define CM_WKUP_CLKCTRL_MODULE_IDLEST_WIDTH		2
#define CM_WKUP_CLKCTRL_MODULE_IDLEST_ENABLED		0
#define CM_WKUP_CLKCTRL_MODULE_IDLEST_TRANS		1
#define CM_WKUP_CLKCTRL_MODULE_IDLEST_IDLE		2
#define CM_WKUP_CLKCTRL_MODULE_IDLEST_DISABLED		3


/*
 * auxiliary function
 */
#define _F_MASK(regname,fieldname) \
    GENMASK(regname##_##fieldname##_SHIFT+regname##_##fieldname##_WIDTH - 1,regname##_##fieldname##_SHIFT)

#define _F_MAX(regname,fieldname) \
    GENMASK(regname##_##fieldname##_SHIFT+regname##_##fieldname##_WIDTH - 1,0)

#define _F_CONST(regname,fieldname,constname) \
    (regname##_##fieldname##_##constname<<regname##_##fieldname##_SHIFT)

#define _F_CHECK(regname,fieldname,valname,value) \
    (((value) &GENMASK(regname##_##fieldname##_SHIFT+regname##_##fieldname##_WIDTH - 1,regname##_##fieldname##_SHIFT)) == \
    (regname##_##fieldname##_##valname<<regname##_##fieldname##_SHIFT))

#define _F_VAR(regname,fieldname,value) \
    (((value)&GENMASK(regname##_##fieldname##_WIDTH - 1,0))<<regname##_##fieldname##_SHIFT)

#define _F_GET(regname,fieldname,value) \
    (((value)>>regname##_##fieldname##_SHIFT)&GENMASK(regname##_##fieldname##_WIDTH - 1,0))



struct am335x_clk_node{
	struct clk_hw clk_hw;
	struct regmap *regmap;
	uint32_t reg;
};
#define to_clk_node(hw) container_of(hw, struct am335x_clk_node, clk_hw)


/*
 * clock gate
 */
static int am335x_clk_ctrl_enable(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);

	regmap_write(clk_node->regmap,clk_node->reg,_F_CONST(CM_WKUP_CLKCTRL,MODULE_MODE,ENABLE));

	return 0;
}
static void am335x_clk_ctrl_disable(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);

	regmap_write(clk_node->regmap,clk_node->reg,_F_CONST(CM_WKUP_CLKCTRL,MODULE_MODE,DISABLE));

}
static int am335x_clk_ctrl_is_enabled(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	uint32_t value = 0;
	int ret;

	ret = regmap_read(clk_node->regmap,clk_node->reg,&value);


	return _F_CHECK(CM_WKUP_CLKCTRL,MODULE_IDLEST,ENABLED,value);
}

static const struct clk_ops am335x_clk_ctrl_ops = {
	.enable = am335x_clk_ctrl_enable,
	.disable = am335x_clk_ctrl_disable,
	.is_enabled = am335x_clk_ctrl_is_enabled,
};
static void __init am335x_clk_ctrl_init(struct device_node *np)
{
	struct clk_init_data init;
	struct am335x_clk_node *clk_node = NULL;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;
	uint32_t reg;


	ret = of_property_read_u32(np,"ti,reg",&reg);
	if(ret < 0){
		pr_err("clk-ctrl %s :missing reg addr",name);
		return ;
	}


	clk_node = kzalloc(sizeof(*clk_node),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-ctrl %s :no memory for alloc clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		pr_err("clk-ctrl %s :no parents find",name);
		return ;
	}

	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		pr_err("clk-ctrl %s :no regmap for clock node",name);
		kfree(clk_node);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_ctrl_ops;

	clk_node->reg = reg;
	clk_node->clk_hw.init = &init;

	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		pr_err("clk-ctrl %s :clock node hw register failed",name);
		kfree(clk_node);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);

}
CLK_OF_DECLARE(am335x_clk_ctrl,"ti,am335x-clk-ctrl",am335x_clk_ctrl_init);


/*
 * dpll clkout
 */

static int am335x_clk_per_pll_clkout_enable(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value;

	regmap_read(clk_node->regmap,CM_DIV_M2_DPLL_PER,&value);

	value &= ~(_F_MASK(CM_DIV_M2_DPLL_PER,GATE));
	value |= _F_CONST(CM_DIV_M2_DPLL_PER,GATE,ALWAYS);

	regmap_write(clk_node->regmap,CM_DIV_M2_DPLL_PER,value);

	return 0;
}

static void am335x_clk_per_pll_clkout_disable(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value;

	regmap_read(clk_node->regmap,CM_DIV_M2_DPLL_PER,&value);

	value &= ~(_F_MASK(CM_DIV_M2_DPLL_PER,GATE));
	value |= _F_CONST(CM_DIV_M2_DPLL_PER,GATE,AUTO);

	regmap_write(clk_node->regmap,CM_DIV_M2_DPLL_PER,value);

}

static int am335x_clk_per_pll_clkout_is_enabled(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value = 0;
	int ret;

	ret = regmap_read(clk_node->regmap,CM_DIV_M2_DPLL_PER,&value);


	return _F_CHECK(CM_DIV_M2_DPLL_PER,STATUS,ENABLED,value);
}


static unsigned long am335x_clk_per_pll_clkout_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value = 0;

	regmap_read(clk_node->regmap,CM_DIV_M2_DPLL_PER,&value);

	value = _F_GET(CM_DIV_M2_DPLL_PER,DIV,value);
	if(!value){
		return parent_rate;
	}


	return DIV_ROUND_UP(parent_rate,value);
}
static int am335x_clk_per_pll_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value;
	unsigned int restore;
	unsigned int div;

	regmap_read(clk_node->regmap,CM_DIV_M2_DPLL_PER,&restore);

	div = divider_get_val(rate, parent_rate, NULL,
			CM_DIV_M2_DPLL_PER_DIV_WIDTH, CLK_DIVIDER_ONE_BASED);

	if(_F_GET(CM_DIV_M2_DPLL_PER,DIV,restore) == div){
		return 0;
	}

	value = restore &~(_F_MASK(CM_DIV_M2_DPLL_PER,DIV));
	value |= _F_VAR(CM_DIV_M2_DPLL_PER,DIV,div);

	regmap_write(clk_node->regmap,CM_DIV_M2_DPLL_PER,value);

	while(((restore ^ value) &
			_F_CONST(CM_DIV_M2_DPLL_PER,DIVCHACK,TRUE))){
		regmap_read(clk_node->regmap,CM_DIV_M2_DPLL_PER,&value);
	}


	return 0;
}
static long am335x_clk_per_pll_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	return divider_round_rate(hw, rate, parent_rate, NULL,
			CM_DIV_M2_DPLL_PER_DIV_WIDTH, CLK_DIVIDER_ONE_BASED);
}

static const struct clk_ops am335x_clk_per_pll_clkout_ops = {
	.enable		= am335x_clk_per_pll_clkout_enable,
	.disable	= am335x_clk_per_pll_clkout_disable,
	.is_enabled	= am335x_clk_per_pll_clkout_is_enabled,
	.round_rate	= am335x_clk_per_pll_clkout_round_rate,
	.recalc_rate	= am335x_clk_per_pll_clkout_recalc_rate,
	.set_rate	= am335x_clk_per_pll_clkout_set_rate,
};
static void __init am335x_clk_per_pll_clkout_init(struct device_node *np)
{

	struct clk_init_data init;
	struct am335x_clk_node *clk_node;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_node = kzalloc(sizeof(*clk_node),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-per-pll-clkout %s :no memory alloc for clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		pr_err("clk-per-pll-clkout %s :parents config err",name);
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		kfree(clk_node);
		pr_err("clk-per-pll-clkout %s :no regmap for clock node",name);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_per_pll_clkout_ops;



	clk_node->clk_hw.init = &init;
	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		pr_err("clk-per-pll-clkout %s :failed to register hw clock",name);
		kfree(clk_node);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);

}
CLK_OF_DECLARE(am335x_clk_per_pll_clkout,"ti,am335x-clk-per-pll-clkout",am335x_clk_per_pll_clkout_init);


static int am335x_clk_core_pll_clkout_enable(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value;

	regmap_read(clk_node->regmap,clk_node->reg,&value);

	value &= ~(_F_MASK(CM_DIV_Mx_DPLL_CORE,GATE)|_F_MASK(CM_DIV_Mx_DPLL_CORE,POWER));
	value |= _F_CONST(CM_DIV_Mx_DPLL_CORE,GATE,ALWAYS)|_F_CONST(CM_DIV_Mx_DPLL_CORE,POWER,AUTO);

	regmap_write(clk_node->regmap,clk_node->reg,value);

	return 0;
}

static void am335x_clk_core_pll_clkout_disable(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value;

	regmap_read(clk_node->regmap,clk_node->reg,&value);

	value &= ~(_F_MASK(CM_DIV_Mx_DPLL_CORE,GATE));
	value |= _F_CONST(CM_DIV_Mx_DPLL_CORE,GATE,AUTO);

	regmap_write(clk_node->regmap,clk_node->reg,value);

}

static int am335x_clk_core_pll_clkout_is_enabled(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value = 0;
	int ret;

	ret = regmap_read(clk_node->regmap,clk_node->reg,&value);


	return _F_CHECK(CM_DIV_Mx_DPLL_CORE,STATUS,ENABLED,value);
}

static unsigned long am335x_clk_core_pll_clkout_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value = 0;

	regmap_read(clk_node->regmap,clk_node->reg,&value);

	value = _F_GET(CM_DIV_Mx_DPLL_CORE,DIV,value);
	if(!value){
		return parent_rate;
	}


	return DIV_ROUND_UP(parent_rate,value);
}

static int am335x_clk_core_pll_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned int value;
	unsigned int restore;
	unsigned int div;


	regmap_read(clk_node->regmap,clk_node->reg,&restore);

	div = divider_get_val(rate, parent_rate, NULL,
			CM_DIV_Mx_DPLL_CORE_DIV_WIDTH, CLK_DIVIDER_ONE_BASED);

	if(_F_GET(CM_DIV_Mx_DPLL_CORE,DIV,restore) == div){
		return 0;
	}

	value = restore &~_F_MASK(CM_DIV_Mx_DPLL_CORE,DIV);
	value |= _F_VAR(CM_DIV_Mx_DPLL_CORE,DIV,div);

	regmap_write(clk_node->regmap,clk_node->reg,value);

	while(((restore ^ value) &
			_F_CONST(CM_DIV_Mx_DPLL_CORE,DIVCHACK,TRUE))){
		regmap_read(clk_node->regmap,clk_node->reg,&value);
		cpu_relax();

	}


	return 0;
}
static long am335x_clk_core_pll_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	return divider_round_rate(hw, rate, parent_rate, NULL,
			CM_DIV_Mx_DPLL_CORE_DIV_WIDTH, CLK_DIVIDER_ONE_BASED);
}



static const struct clk_ops am335x_clk_core_pll_clkout_ops = {
	.enable		= am335x_clk_core_pll_clkout_enable,
	.disable	= am335x_clk_core_pll_clkout_disable,
	.is_enabled	= am335x_clk_core_pll_clkout_is_enabled,
	.round_rate	= am335x_clk_core_pll_clkout_round_rate,
	.recalc_rate	= am335x_clk_core_pll_clkout_recalc_rate,
	.set_rate	= am335x_clk_core_pll_clkout_set_rate,
};

static void __init am335x_clk_core_pll_clkout_init(struct device_node *np)
{
	struct clk_init_data init;
	struct am335x_clk_node *clk_node;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	uint32_t reg;
	int num_parents;
	int ret;

	ret = of_property_read_u32(np,"ti,reg",&reg);
	if(ret < 0){
		pr_err("clk-ctrl %s :missing reg addr",name);
		return ;
	}


	clk_node = kzalloc(sizeof(*clk_node),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-per-pll-clkout %s :no memory alloc for clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		pr_err("clk-per-pll-clkout %s :parents config err",name);
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		kfree(clk_node);
		pr_err("clk-per-pll-clkout %s :no regmap for clock node",name);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_core_pll_clkout_ops;



	clk_node->clk_hw.init = &init;
	clk_node->regmap = regmap;
	clk_node->reg = reg;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		pr_err("clk-per-pll-clkout %s :failed to register hw clock",name);
		kfree(clk_node);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);


}
CLK_OF_DECLARE(am335x_clk_core_pll_clkout,"ti,am335x-clk-core-pll-clkout",am335x_clk_core_pll_clkout_init);


/*
 * dpll clk
 */

static int am335x_clk_per_pll_prepare(struct clk_hw *hw)
{
	return 0;
}

static void am335x_clk_per_pll_unprepare(struct clk_hw *hw)
{
}

static int am335x_clk_per_pll_is_prepared(struct clk_hw *hw)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	uint32_t value;

	regmap_read(clk_node->regmap,CM_IDLEST_DPLL_PER,&value);

	return _F_CHECK(CM_IDLEST_DPLL_PER,ST_DPLL_CLK,LOCKED,value);
}

static unsigned long am335x_clk_per_pll_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	uint32_t value;
	int mult;
	int div;


	regmap_read(clk_node->regmap,CM_CLKSEL_DPLL_PERIPH,&value);

	mult = _F_GET(CM_CLKSEL_DPLL_PERIPH,DPLL_MULT,value);
	div = _F_GET(CM_CLKSEL_DPLL_PERIPH,DPLL_DIV,value);


	return DIV_ROUND_UP(parent_rate*mult,div+1);
}

static int am335x_clk_per_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	unsigned long div,mult;
	uint32_t value;



	rational_best_approximation(rate,parent_rate,
			_F_MAX(CM_CLKSEL_DPLL_PERIPH,DPLL_MULT),
			_F_MAX(CM_CLKSEL_DPLL_PERIPH,DPLL_DIV),
			&mult,&div);


	regmap_update_bits(clk_node->regmap,CM_CLKMODE_DPLL_PER,
			_F_MASK(CM_CLKMODE_DPLL_PER,DPLL_EN),
			_F_CONST(CM_CLKMODE_DPLL_PER,DPLL_EN,BYPASS));


	do {
		regmap_read(clk_node->regmap,CM_IDLEST_DPLL_PER,&value);
	} while(!(_F_CHECK(CM_IDLEST_DPLL_PER,ST_MN_BYPASS,TRUE,value) &&
			_F_CHECK(CM_IDLEST_DPLL_PER,ST_DPLL_CLK,UNLOCKED,value)));


	value = _F_VAR(CM_CLKSEL_DPLL_PERIPH,DPLL_MULT,mult) |
			_F_VAR(CM_CLKSEL_DPLL_PERIPH,DPLL_DIV,div-1);

	regmap_write(clk_node->regmap,CM_CLKSEL_DPLL_PERIPH,value);

	value = _F_VAR(CM_DIV_M2_DPLL_PER,DIV,5);

	regmap_write(clk_node->regmap,CM_DIV_M2_DPLL_PER,value);

	regmap_update_bits(clk_node->regmap,CM_CLKMODE_DPLL_PER,
			_F_MASK(CM_CLKMODE_DPLL_PER,DPLL_EN),
			_F_CONST(CM_CLKMODE_DPLL_PER,DPLL_EN,ENABLE));

	do {
		regmap_read(clk_node->regmap,CM_IDLEST_DPLL_PER,&value);
	} while(!(_F_CHECK(CM_IDLEST_DPLL_PER,ST_MN_BYPASS,FALSE,value) &&
			_F_CHECK(CM_IDLEST_DPLL_PER,ST_DPLL_CLK,LOCKED,value)));

	return 0;
}

static long am335x_clk_per_pll_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	unsigned long div,mult;
	u64 ret;

	rational_best_approximation(rate,*parent_rate,
			_F_MAX(CM_CLKSEL_DPLL_PERIPH,DPLL_MULT),
			_F_MAX(CM_CLKSEL_DPLL_PERIPH,DPLL_DIV),
			&mult,&div);

	ret = *parent_rate * mult;
	do_div(ret,div);
	return ret ;
}

static const struct clk_ops am335x_clk_per_pll_ops = {
	.prepare	= am335x_clk_per_pll_prepare,
	.unprepare	= am335x_clk_per_pll_unprepare,
	.is_prepared	= am335x_clk_per_pll_is_prepared,
	.recalc_rate	= am335x_clk_per_pll_recalc_rate,
	.round_rate	= am335x_clk_per_pll_round_rate,
	.set_rate	= am335x_clk_per_pll_set_rate,
};

static void __init am335x_clk_per_pll_init(struct device_node *np)
{
	struct clk_init_data init;
	struct am335x_clk_node *clk_node;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_node = kzalloc(sizeof(*clk_node),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-per-pll %s :no memory alloc for clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		pr_err("clk-per-pll %s :parents config err",name);
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		kfree(clk_node);
		pr_err("clk-per-pll %s :no regmap for clock node",name);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_per_pll_ops;



	clk_node->clk_hw.init = &init;
	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		pr_err("clk-per-pll %s :failed to register hw clock",name);
		kfree(clk_node);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);

}
CLK_OF_DECLARE(am335x_clk_per_pll,"ti,am335x-clk-per-pll",am335x_clk_per_pll_init);


static unsigned long am335x_clk_core_pll_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	uint32_t value;
	uint64_t rate;
	int mult;
	int div;


	regmap_read(clk_node->regmap,CM_CLKSEL_DPLL_CORE,&value);

	mult = _F_GET(CM_CLKSEL_DPLL_CORE,DPLL_MULT,value);
	div = _F_GET(CM_CLKSEL_DPLL_CORE,DPLL_DIV,value);


	rate = (uint64_t)parent_rate *mult;
	rate = DIV_ROUND_UP_ULL(rate,div+1);
	return rate;
}


static const struct clk_ops am335x_clk_core_pll_ops = {
	.recalc_rate	= am335x_clk_core_pll_recalc_rate,
};

static void __init am335x_clk_core_pll_init(struct device_node *np)
{
	struct clk_init_data init;
	struct am335x_clk_node *clk_node;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_node = kzalloc(sizeof(*clk_node),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-core-pll %s :no memory alloc for clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		pr_err("clk-core-pll %s :parents config err",name);
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		kfree(clk_node);
		pr_err("clk-core-pll %s :no regmap for clock node",name);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_core_pll_ops;



	clk_node->clk_hw.init = &init;
	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		pr_err("clk-core-pll %s :failed to register hw clock",name);
		kfree(clk_node);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);


}
CLK_OF_DECLARE(am335x_clk_core_pll,"ti,am335x-clk-core-pll",am335x_clk_core_pll_init);
