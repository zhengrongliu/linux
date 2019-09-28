/*
 * clk-am335x-common.c
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
#include <linux/mfd/syscon.h>




struct am335x_clk_divider_node{
	struct clk_divider clk_div;
	struct regmap *regmap;
	struct clk_div_table div_table[0];
};
#define to_clk_divider_node(hw) container_of(hw, struct am335x_clk_divider_node, clk_div)

struct am335x_clk_gate_node{
	struct clk_hw clk_hw;
	struct regmap *regmap;
	uint32_t reg;
	uint32_t flags;
	uint32_t shift;
};
#define to_clk_gate_node(hw) container_of(hw, struct am335x_clk_gate_node, clk_hw)

struct am335x_clk_mux_node{
	struct clk_hw clk_hw;
	struct regmap *regmap;
	uint32_t reg;
	uint32_t flags;
	uint32_t shift;
	uint32_t width;
	uint32_t table[0];
};
#define to_clk_mux_node(hw) container_of(hw, struct am335x_clk_mux_node, clk_hw)

static uint8_t am335x_clk_mux_get_parent(struct clk_hw *hw)
{
	struct am335x_clk_mux_node *clk_node = to_clk_mux_node(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	uint32_t value = 0;
	int ret;
	int i;

	ret = regmap_read(clk_node->regmap,clk_node->reg,&value);
	if(ret < 0){
		return ret;
	}

	value = (value>>clk_node->shift)&(BIT(clk_node->width)-1);

	if(clk_node->table){
		for(i = 0;i<num_parents;i++){
			if(clk_node->table[i] == value)
				return i;
		}
		return -EINVAL;
	}

	if (value && (clk_node->flags & CLK_MUX_INDEX_BIT))
		value = ffs(value) - 1;

	if (value && (clk_node->flags & CLK_MUX_INDEX_ONE))
		value--;

	if (value >= num_parents)
		return -EINVAL;

	return value;
}

static int am335x_clk_mux_set_parent(struct clk_hw *hw, uint8_t index)
{
	struct am335x_clk_mux_node *clk_node = to_clk_mux_node(hw);
	int ret;

	if (clk_node->table) {
		index = clk_node->table[index];
	} else {
		if (clk_node->flags & CLK_MUX_INDEX_BIT)
			index = 1 << index;

		if (clk_node->flags & CLK_MUX_INDEX_ONE)
			index++;
	}


	ret = regmap_update_bits(clk_node->regmap, clk_node->reg,
			GENMASK(clk_node->shift+clk_node->width-1,clk_node->shift),
			index<<clk_node->shift);


	return ret;
}

const struct clk_ops am335x_clk_mux_ops = {
	.get_parent = am335x_clk_mux_get_parent,
	.set_parent = am335x_clk_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};
static void __init am335x_clk_mux_init(struct device_node *np)
{

	struct clk_init_data init;
	struct am335x_clk_mux_node *clk_node = NULL;
	struct regmap *regmap;
	const char *name = np->name;
	const char **parent_names = NULL;
	int num_parents;
	int ret;
	int i;
	uint32_t shift,flags,width,reg,value ;


	ret = of_property_read_u32(np,"ti,mux-flags",&flags);
	if(ret < 0){
		flags = 0;
	}

	ret = of_property_read_u32(np,"ti,mux-shift",&shift);
	if(ret < 0){
		pr_err("clk-mux %s :missing field shift",name);
		return ;
	}

	ret = of_property_read_u32(np,"ti,mux-width",&width);
	if(ret < 0){
		pr_err("clk-mux %s :missing field width",name);
		return ;
	}


	ret = of_property_read_u32(np,"ti,reg",&reg);
	if(ret < 0){
		pr_err("clk-mux %s :missing field reg addr",name);
		return ;
	}


	value = 0;
	of_get_property(np, "ti,mux-selectors", &value);
	num_parents = of_clk_get_parent_count(np);

	if((value && num_parents != value) || !num_parents){
		pr_err("clk-mux %s :parents do not match selectors or no parents specific",name);
		return ;
	}


	clk_node = kzalloc(sizeof(*clk_node) + num_parents * sizeof(clk_node->table[0]),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-mux %s :no memory for alloc clock node",name);
		return ;
	}


	parent_names = kzalloc(sizeof(*parent_names) * num_parents,GFP_KERNEL);
	if(!parent_names){
		pr_err("clk-mux %s :no memory for alloc parent name",name);
		goto failed_return;
	}


	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		pr_err("clk-mux %s :regmap failed",name);
		goto failed_return;
	}

	of_property_read_string(np,"clock-output-names",&name);

	for (i = 0; i < num_parents; i++) {
                of_property_read_u32_index(np, "ti,mux-selectors", i, &value);
                clk_node->table[i] = value;
        }


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_mux_ops;

	clk_node->reg = reg;
	clk_node->shift = shift;
	clk_node->flags = flags;
	clk_node->clk_hw.init = &init;

	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		pr_err("clk-mux %s :failed to register hw clock",name);
		goto failed_return;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);

	kfree(parent_names);
	return ;
failed_return:
	if(!clk_node){
		kfree(clk_node);
	}

	if(!parent_names){
		kfree(parent_names);
	}

}
CLK_OF_DECLARE(am335x_clk_mux,"ti,am335x-clk-mux",am335x_clk_mux_init);

static int am335x_clk_gate_enable(struct clk_hw *hw)
{
	struct am335x_clk_gate_node *clk_node = to_clk_gate_node(hw);
	int ret;


	if(clk_node->flags & CLK_GATE_SET_TO_DISABLE){
		ret = regmap_update_bits(clk_node->regmap, clk_node->reg,BIT(clk_node->shift),
			0);

	} else {
		ret = regmap_update_bits(clk_node->regmap, clk_node->reg,BIT(clk_node->shift),
			BIT(clk_node->shift));
	}
	return ret;
}
static void am335x_clk_gate_disable(struct clk_hw *hw)
{
	struct am335x_clk_gate_node *clk_node = to_clk_gate_node(hw);

	if(clk_node->flags & CLK_GATE_SET_TO_DISABLE){
		regmap_update_bits(clk_node->regmap, clk_node->reg,BIT(clk_node->shift),
			BIT(clk_node->shift));

	} else {
		regmap_update_bits(clk_node->regmap, clk_node->reg,BIT(clk_node->shift),
			0);
	}
}
static int am335x_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct am335x_clk_gate_node *clk_node = to_clk_gate_node(hw);
	uint32_t value = 0;
	int ret;

	ret = regmap_read(clk_node->regmap,clk_node->reg,&value);
	if(ret){
		return ret;
	}

	value &= BIT(clk_node->shift);

	value = clk_node->flags & CLK_GATE_SET_TO_DISABLE?!value:!!value;

	return value;
}

static const struct clk_ops am335x_clk_gate_ops = {
	.enable = am335x_clk_gate_enable,
	.disable = am335x_clk_gate_disable,
	.is_enabled = am335x_clk_gate_is_enabled,
};
static void __init am335x_clk_gate_init(struct device_node *np)
{
	struct clk_init_data init;
	struct am335x_clk_gate_node *clk_node = NULL;
	struct regmap *regmap;
	const char * *parent_names = NULL;
	const char *name = np->name;
	int num_parents;
	int ret;
	uint32_t shift,flags,reg;

	ret = of_property_read_u32(np,"ti,gate-flags",&flags);
	if(ret < 0){
		flags = 0;
	}

	ret = of_property_read_u32(np,"ti,gate-shift",&shift);
	if(ret < 0){
		pr_err("clk-gate %s :missing field shift",name);
		return ;
	}


	ret = of_property_read_u32(np,"ti,reg",&reg);
	if(ret < 0){
		pr_err("clk-gate %s :missing reg addr",name);
		return ;
	}


	clk_node = kzalloc(sizeof(*clk_node),GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-gate %s :no memory for alloc clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0){
		pr_err("clk-gate %s :no parents find",name);
		goto failed_return;
	}

	parent_names = kzalloc(sizeof(*parent_names) * num_parents,GFP_KERNEL);
	if(!parent_names){
		pr_err("clk-gate %s :no memory for alloc parent name",name);
		goto failed_return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		pr_err("clk-gate %s :no regmap for clock node",name);
		goto failed_return;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_gate_ops;

	clk_node->reg = reg;
	clk_node->shift = shift;
	clk_node->flags = flags;
	clk_node->clk_hw.init = &init;

	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_hw);
	if(ret){
		pr_err("clk-gate %s :clock node hw register failed",name);
		goto failed_return;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_hw);
	kfree(parent_names);
	return ;

failed_return:
	if(!clk_node){
		kfree(clk_node);
	}

	if(!parent_names){
		kfree(parent_names);
	}

}
CLK_OF_DECLARE(am335x_clk_gate,"ti,am335x-clk-gate",am335x_clk_gate_init);

static unsigned long am335x_clk_divider_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_divider *clk_div = to_clk_divider(hw);
	struct am335x_clk_divider_node *clk_node = to_clk_divider_node(clk_div);
	uint32_t value;
	int ret;

	ret = regmap_read(clk_node->regmap,(uint32_t)clk_div->reg,&value);
	if(ret){
		return ret;
	}

	value >>= clk_div->shift;
	value &= (1<<clk_div->width)-1;


	return divider_recalc_rate(hw,parent_rate,value,clk_div->table,clk_div->flags,clk_div->width);
}

static int am335x_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct clk_divider *clk_div = to_clk_divider(hw);
	struct am335x_clk_divider_node *clk_node = to_clk_divider_node(clk_div);
	uint32_t value;
	int ret;

	value = divider_get_val(rate, parent_rate, clk_div->table,
				clk_div->width, clk_div->flags);

	ret = regmap_update_bits(clk_node->regmap, (uint32_t)clk_div->reg,
			GENMASK(clk_div->shift+clk_div->width-1,clk_div->shift),
			value << clk_div->shift);

	return ret;
}

static long am335x_clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	struct clk_divider *clk_div = to_clk_divider(hw);

	return divider_round_rate(hw, rate, parent_rate, clk_div->table,
				  clk_div->width, clk_div->flags);
}


static const struct clk_ops am335x_clk_divider_ops = {
	.round_rate	= am335x_clk_divider_round_rate,
	.recalc_rate	= am335x_clk_divider_recalc_rate,
	.set_rate	= am335x_clk_divider_set_rate,
};

static void __init am335x_clk_divider_init(struct device_node *np)
{
	struct clk_init_data init;
	struct am335x_clk_divider_node *clk_node = NULL;
	struct regmap *regmap;
	const char **parent_names = NULL;
	const char *name = np->name;
	int num_parents;
	int ret;
	int size;
	int i;
	uint32_t num_div = 0;
	uint32_t valid_div = 0;
	uint32_t value,width,shift,flags,reg;



	ret = of_property_read_u32(np,"ti,divider-flags",&flags);
	if(ret < 0){
		flags = 0;
	}

	ret = of_property_read_u32(np,"ti,divider-shift",&shift);
	if(ret < 0){
		pr_err("clk-divider %s :missing field shift",name);
		return ;
	}

	ret = of_property_read_u32(np,"ti,divider-width",&width);
	if(ret < 0){
		pr_err("clk-divider %s :missing field width",name);
		return ;
	}



	ret = of_property_read_u32(np,"ti,reg",&reg);
	if(ret < 0){
		pr_err("clk-divider %s :missing reg addr",name);
		return ;
	}

	of_get_property(np, "ti,divider-divisors", &num_div);

        for (i = 0; i < num_div; i++) {
                of_property_read_u32_index(np, "ti,divider-divisors", i, &value);
                if (value)
                        valid_div++;
        }


	if(valid_div != 0 ){
		size = (valid_div+1)*sizeof(clk_node->div_table[0]);
	} else {
		size = 0;
	}



	clk_node = kzalloc(sizeof(*clk_node) + size,GFP_KERNEL);
	if(!clk_node){
		pr_err("clk-divider %s :no memory alloc for clock node",name);
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0){
		pr_err("clk-divider %s :no parents find",name);
		goto failed_return;
	}

	parent_names = kzalloc(sizeof(*parent_names) * num_parents,GFP_KERNEL);
	if(!parent_names){
		pr_err("clk-divider %s :no memory for alloc parent name",name);
		goto failed_return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_regmap_lookup_by_phandle(np,"syscon");
	if(IS_ERR(regmap)){
		pr_err("clk-divider %s :no regmap for clock node",name);
		goto failed_return;
	}

	of_property_read_string(np,"clock-output-names",&name);

	valid_div = 0;
	for (i = 0; i < num_div; i++) {
                of_property_read_u32_index(np, "ti,divider-divisors", i, &value);
                if(value){
                	clk_node->div_table[valid_div].val = i;
                	clk_node->div_table[valid_div].div = value;
                	valid_div++;
                }
        }


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &am335x_clk_divider_ops;

	clk_node->clk_div.table = clk_node->div_table;
	clk_node->clk_div.reg = (void * __iomem)reg;
	clk_node->clk_div.shift = shift;
	clk_node->clk_div.width = width;
	clk_node->clk_div.flags = flags;
	clk_node->clk_div.hw.init = &init;

	clk_node->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_node->clk_div.hw);
	if(ret){
		pr_err("clk-divider %s :failed to register hw clock",name);
		goto failed_return;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_node->clk_div.hw);

	kfree(parent_names);
	return ;

failed_return:
	if(!clk_node){
		kfree(clk_node);
	}

	if(!parent_names){
		kfree(parent_names);
	}


}
CLK_OF_DECLARE(am335x_clk_divider,"ti,am335x-clk-divider",am335x_clk_divider_init);


