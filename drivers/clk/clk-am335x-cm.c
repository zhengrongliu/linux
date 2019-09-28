/*
 * clk-am335x-cm.c
 *
 *  Created on: Mar 16, 2018
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


#define CM_CLK32K_DIVRATIO_CTRL 0x444

/*
 * CM_CLK32K_DIVRATIO_CTRL register
 */
#define CM_CLK32K_DIVRATIO_CTRL_OPP50_EN_SHIFT 		0
#define CM_CLK32K_DIVRATIO_CTRL_OPP50_EN_WIDTH 		1
#define CM_CLK32K_DIVRATIO_CTRL_OPP50_EN_ENABLE		1
#define CM_CLK32K_DIVRATIO_CTRL_OPP50_EN_DISABLE	0



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
};
#define to_clk_node(hw) container_of(hw, struct am335x_clk_node, clk_hw)


static unsigned long  clk_32k_div_table[] = {
	7324219,3662109
};
static unsigned long am335x_clk_32K_divider_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	uint64_t rate = parent_rate;
	uint32_t value;

	rate *= 10000;

	regmap_read(clk_node->regmap,CM_CLK32K_DIVRATIO_CTRL,&value);

	if(_F_CHECK(CM_CLK32K_DIVRATIO_CTRL,OPP50_EN,DISABLE,value)){
		rate = DIV_ROUND_UP_ULL(rate,clk_32k_div_table[0]);
	} else {
		rate = DIV_ROUND_UP_ULL(rate,clk_32k_div_table[1]);
	}

	return rate;

}

static int am335x_clk_32K_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct am335x_clk_node *clk_node = to_clk_node(hw);
	uint64_t rate_0 = parent_rate;
	uint64_t rate_1;

	rate_0 *= 10000;
	rate_1 = rate_0;

	DIV_ROUND_UP_ULL(rate_0,clk_32k_div_table[0]);
	DIV_ROUND_UP_ULL(rate_1,clk_32k_div_table[1]);

	if(abs(rate_0 - rate) < abs(rate_1 - rate)){
		regmap_write(clk_node->regmap,CM_CLK32K_DIVRATIO_CTRL,
				_F_CONST(CM_CLK32K_DIVRATIO_CTRL,OPP50_EN,DISABLE));
	} else {
		regmap_write(clk_node->regmap,CM_CLK32K_DIVRATIO_CTRL,
				_F_CONST(CM_CLK32K_DIVRATIO_CTRL,OPP50_EN,ENABLE));
	}


	return 0;
}

static long am335x_clk_32K_divider_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	uint64_t rate_0 = *parent_rate;
	uint64_t rate_1;

	rate_0 *= 10000;
	rate_1 = rate_0;


	DIV_ROUND_UP_ULL(rate_0,clk_32k_div_table[0]);
	DIV_ROUND_UP_ULL(rate_1,clk_32k_div_table[1]);


	if(abs(rate_0 - rate) < abs(rate_1 - rate)){
		return rate_0;
	} else {
		return rate_1;
	}


}


static const struct clk_ops am335x_clk_32K_divider_ops = {
	.round_rate	= am335x_clk_32K_divider_round_rate,
	.recalc_rate	= am335x_clk_32K_divider_recalc_rate,
	.set_rate	= am335x_clk_32K_divider_set_rate,
};

static void __init am335x_clk_32K_init(struct device_node *np)
{
	struct clk_init_data init;
	struct am335x_clk_node *clk_node = NULL;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_node = kzalloc(sizeof(*clk_node),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-32K %s :no memory alloc for clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		pr_err("clk-32K %s :parents config err",name);
		return;
	}

	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		kfree(clk_node);
		pr_err("clk-32K %s :no regmap for clock node",name);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_32K_divider_ops;


	clk_node->clk_hw.init = &init;
	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		kfree(clk_node);
		pr_err("clk-32K %s :failed to register hw clock",name);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);

}
CLK_OF_DECLARE(am335x_clk_32K,"ti,am335x-clk-32K",am335x_clk_32K_init);




