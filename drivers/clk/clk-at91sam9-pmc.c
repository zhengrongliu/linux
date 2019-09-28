/*
 * clk-at91sam9-pmc.c
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
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#define SLOW_CLOCK_FREQ		32768
#define MAINF_DIV		16
#define PLLA_MAX_FREQ           800000000
#define PLLA_MIN_FREQ           400000000

/*
 * PMC registers offset
 */
#define PMC_SCER        0x00
#define PMC_SCDR        0x04
#define PMC_SCSR        0x08
#define PMC_PCER        0x10
#define PMC_PCDR        0x14
#define PMC_PCSR        0x18
#define CKGR_UCKR       0x1c
#define CKGR_MOR        0x20
#define CKGR_MCFR       0x24
#define CKGR_PLLAR      0x28
#define PMC_MCKR        0x30
#define PMC_USB         0x38
#define PMC_SMD         0x3c
#define PMC_PCK0        0x40
#define PMC_PCK1        0x44
#define PMC_IER         0x60
#define PMC_IDR         0x64
#define PMC_SR          0x68
#define PMC_IMR         0x6c
#define PMC_PLLICPR     0x80
#define PMC_WPMR        0xe4
#define PMC_WPSR        0xe8
#define PMC_PCR         0x10c

/*
 * Main oscillator register
 */
#define CKGR_MOR_MOSCXTEN_SHIFT         0
#define CKGR_MOR_MOSCXTBY_SHIFT         1
#define CKGR_MOR_MOSCRCEN_SHIFT         3
#define CKGR_MOR_MUSTZERO_SHIFT         4
#define CKGR_MOR_MOSCXTST_SHIFT         8
#define CKGR_MOR_KEY_SHIFT              16
#define CKGR_MOR_MOSCSEL_SHIFT          24
#define CKGR_MOR_CFDEN_SHIFT            25

#define CKGR_MOR_MOSCXTEN_MASK          1
#define CKGR_MOR_MOSCXTEN_ENABLE        1
#define CKGR_MOR_MOSCXTEN_DISABLE       0
#define CKGR_MOR_MOSCXTBY_MASK          1
#define CKGR_MOR_MOSCXTBY_ENABLE        1
#define CKGR_MOR_MOSCXTBY_DISABLE       0
#define CKGR_MOR_MOSCRCEN_MASK          1
#define CKGR_MOR_MOSCRCEN_ENABLE        1
#define CKGR_MOR_MOSCRCEN_DISABLE       0
#define CKGR_MOR_MUSTZERO_MASK          0x7
#define CKGR_MOR_MUSTZERO_VALID         0x0
#define CKGR_MOR_MOSCXTST_MASK          0xff
#define CKGR_MOR_MOSCXTST_MAX           0xff
#define CKGR_MOR_KEY_MASK               0xff
#define CKGR_MOR_KEY_PASSWD             (0x37)
#define CKGR_MOR_MOSCSEL_MASK           1
#define CKGR_MOR_MOSCSEL_XTAL           1
#define CKGR_MOR_MOSCSEL_RC             0
#define CKGR_MOR_CFDEN_MASK             1
#define CKGR_MOR_CFDEN_ENABLE           1
#define CKGR_MOR_CFDEN_DISABLE          0

/*
 * Clock Generator PLLA register
 */
#define CKGR_PLLAR_DIVA_SHIFT           0
#define CKGR_PLLAR_DIVA_MASK            0xff
#define CKGR_PLLAR_DIVA_MAX             0xff
#define CKGR_PLLAR_DIVA_MIN             0x1
#define CKGR_PLLAR_PLLACOUNT_SHIFT      8
#define CKGR_PLLAR_PLLACOUNT_MASK       0x3f
#define CKGR_PLLAR_PLLACOUNT_MAX        0x3f
#define CKGR_PLLAR_OUTA_SHIFT           14
#define CKGR_PLLAR_OUTA_MASK            0x3
#define CKGR_PLLAR_MULA_SHIFT           16
#define CKGR_PLLAR_MULA_MASK            0xff
#define CKGR_PLLAR_MULA_MAX             0xff
#define CKGR_PLLAR_MULA_MIN             0x1
#define CKGR_PLLAR_ONE_SHIFT            29
#define CKGR_PLLAR_ONE_MASK             1
#define CKGR_PLLAR_ONE_VALID            1

/*
 * Clock Generator Main Clock Frequency Register
 */
#define CKGR_MCFR_MAINFRDY_SHIFT        16
#define CKGR_MCFR_MAINFRDY_MASK         1
#define CKGR_MCFR_MAINFRDY_READY        1
#define CKGR_MCFR_MAINFRDY_NOTRDY       0
#define CKGR_MCFR_MAINF_SHIFT           0
#define CKGR_MCFR_MAINF_MASK            0xffff

/*
 * PLL charge Pump Current register
 */
#define PMC_PLLICPR_ICPLLA_SHIFT        0
#define PMC_PLLICPR_ICPLLA_MASK         1

/*
 * UTMI clock configuration register
 */
#define CKGR_UCKR_UPLLEN_SHIFT          16
#define CKGR_UCKR_UPLLCOUNT_SHIFT       20
#define CKGR_UCKR_BIASEN_SHIFT          24
#define CKGR_UCKR_BIASCOUNT_SHIFT       28

#define CKGR_UCKR_UPLLEN_MASK           1
#define CKGR_UCKR_UPLLEN_ENABLE         1
#define CKGR_UCKR_UPLLEN_DISABLE        0
#define CKGR_UCKR_UPLLCOUNT_MASK        0xf
#define CKGR_UCKR_UPLLCOUNT_MAX         0xf
#define CKGR_UCKR_BIASCOUNT_MASK        0xf
#define CKGR_UCKR_BIASCOUNT_MAX         0xf

/*
 * PMC Master Clock register
 */
#define PMC_MCKR_CSS_SHIFT              0
#define PMC_MCKR_PRES_SHIFT             4
#define PMC_MCKR_MDIV_SHIFT             8
#define PMC_MCKR_PLLADIV2_SHIFT         12

#define PMC_MCKR_CSS_MASK               0x3
#define PMC_MCKR_PRES_MASK              0x7
#define PMC_MCKR_PRES_WIDTH             0x3
#define PMC_MCKR_MDIV_MASK              0x3
#define PMC_MCKR_MDIV_WIDTH             0x2
#define PMC_MCKR_PLLADIV2_MASK          0x1
#define PMC_MCKR_PLLADIV2_WIDTH         0x1

/*
 * PMC Status register
 */
#define PMC_SR_MOSCXTS                  (1<<0)
#define PMC_SR_LOCKA                    (1<<1)
#define PMC_SR_MCKRDY                   (1<<3)
#define PMC_SR_LOCKU                    (1<<6)
#define PMC_SR_OSCSELS                  (1<<7)
#define PMC_SR_PCKRDY0                  (1<<8)
#define PMC_SR_PCKRDY1                  (1<<9)
#define PMC_SR_MOSCSELS                 (1<<16)
#define PMC_SR_MOSCRCS                  (1<<17)
#define PMC_SR_CFDEV                    (1<<18)
#define PMC_SR_CFDS                     (1<<19)
#define PMC_SR_FOS                      (1<<20)


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

struct pmc_clk_main_rc_osc{
	struct clk_hw hw;
	struct regmap *regmap;
	uint32_t frequency;
	uint32_t accuracy;
};
#define to_pmc_clk_main_rc_osc(hw) container_of(hw, struct pmc_clk_main_rc_osc, hw)

struct pmc_clk_main_osc{
	struct clk_hw hw;
	struct regmap *regmap;
};
#define to_pmc_clk_main_osc(hw) container_of(hw, struct pmc_clk_main_osc, hw)

struct pmc_clk_main{
	struct clk_hw hw;
	struct regmap *regmap;
};
#define to_pmc_clk_main(hw) container_of(hw, struct pmc_clk_main, hw)

struct pmc_clk_plla{
	struct clk_hw hw;
	struct regmap *regmap;
	uint8_t mul;
	uint8_t div;
	uint8_t icplla;
	uint8_t outa;
};
#define to_pmc_clk_plla(hw) container_of(hw, struct pmc_clk_plla, hw)

struct pmc_clk_plla_div{
	struct clk_hw hw;
	struct regmap *regmap;
};
#define to_pmc_clk_plla_div(hw) container_of(hw, struct pmc_clk_plla_div, hw)

struct pmc_clk_mck {
	struct clk_hw hw;
	struct regmap *regmap;
};
#define to_pmc_clk_mck(hw) container_of(hw, struct pmc_clk_mck, hw)

struct pmc_clk_mck_div {
	struct clk_hw hw;
	struct regmap *regmap;
};
#define to_pmc_clk_mck_div(hw) container_of(hw, struct pmc_clk_mck_div, hw)

struct pll_param{
	unsigned long min;
	unsigned long max;
	int icplla;
	int outa;
};

static struct pll_param pll_params[] = {
	{745000000,800000000,0,0},
	{695000000,750000000,0,1},
	{645000000,700000000,0,2},
	{595000000,650000000,0,3},
	{545000000,600000000,1,0},
	{495000000,550000000,1,1},
	{445000000,500000000,1,2},
	{400000000,450000000,1,3},
};

static const struct clk_div_table mck_pres_table [] = {
	{0,1},{1,2},{2,4},{3,8},
	{4,16},{5,32},{6,64},{7,3},
	{0,0}
};

static const struct clk_div_table mck_mdiv_table [] = {
	{0,1},{1,2},{2,4},{3,3},{0,0},
};


static unsigned long pmc_clk_mck_div_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct pmc_clk_mck_div  *clk_data = to_pmc_clk_mck_div(hw);
	uint32_t value;
	int i = 0;
	int val;

	regmap_read(clk_data->regmap,PMC_MCKR,&value);

	val = __FIELD_VALUE_GET(PMC_MCKR,MDIV,value);

	for(i = 0;i < ARRAY_SIZE(mck_mdiv_table);i++){
		if(mck_mdiv_table[i].val == val){
			break;
		}
	}

	return DIV_ROUND_UP_ULL((uint64_t)parent_rate, mck_mdiv_table[i].div);
}
static long pmc_clk_mck_div_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{

	return divider_round_rate(hw, rate, parent_rate, mck_mdiv_table,
				 PMC_MCKR_MDIV_WIDTH, 0);

}
static int pmc_clk_mck_div_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct pmc_clk_mck_div  *clk_data = to_pmc_clk_mck_div(hw);
	int div;

	div = divider_get_val(rate, parent_rate, mck_mdiv_table,
				PMC_MCKR_MDIV_WIDTH, 0);

	regmap_update_bits(clk_data->regmap, PMC_MCKR,
			__FIELD_MASK(PMC_MCKR,MDIV),
			__FIELD_VALUE_SET(PMC_MCKR,MDIV,div));
	return 0;
}



static const struct clk_ops pmc_clk_mck_div_ops = {
	.recalc_rate = pmc_clk_mck_div_recalc_rate,
	.round_rate = pmc_clk_mck_div_round_rate,
	.set_rate = pmc_clk_mck_div_set_rate,
};

static void __init pmc_clk_mck_div_init(struct device_node *np)
{
	struct clk_init_data init;
	struct pmc_clk_plla_div *clk_data;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(!clk_data){
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if(IS_ERR(regmap)){
		kfree(clk_data);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &pmc_clk_mck_div_ops;



	clk_data->hw.init = &init;
	clk_data->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);


}
CLK_OF_DECLARE(at91sam9_pmc_clk_mck_div,"atmel,at91sam9-clk-mck-div",pmc_clk_mck_div_init);

static unsigned long pmc_clk_mck_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct pmc_clk_mck  *clk_data = to_pmc_clk_mck(hw);
	uint32_t value;
	int i = 0;
	int val;

	regmap_read(clk_data->regmap,PMC_MCKR,&value);

	val = __FIELD_VALUE_GET(PMC_MCKR,PRES,value);

	for(i = 0;i < ARRAY_SIZE(mck_pres_table);i++){
		if(mck_pres_table[i].val == val){
			break;
		}
	}

	return DIV_ROUND_UP_ULL((uint64_t)parent_rate, mck_pres_table[i].div);
}
static long pmc_clk_mck_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{

	return divider_round_rate(hw, rate, parent_rate, mck_pres_table,
				 PMC_MCKR_PRES_WIDTH, 0);

}
static int pmc_clk_mck_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct pmc_clk_mck  *clk_data = to_pmc_clk_mck(hw);
	int div;

	div = divider_get_val(rate, parent_rate, mck_pres_table,
				PMC_MCKR_PRES_WIDTH, 0);

	regmap_update_bits(clk_data->regmap, PMC_MCKR,
			__FIELD_MASK(PMC_MCKR,PRES),
			__FIELD_VALUE_SET(PMC_MCKR,PRES,div));
	return 0;
}

static uint8_t pmc_clk_mck_get_parent(struct clk_hw *hw)
{
	struct pmc_clk_mck  *clk_data = to_pmc_clk_mck(hw);
	uint32_t value;

	regmap_read(clk_data->regmap,PMC_MCKR,&value);

	return __FIELD_VALUE_GET(PMC_MCKR,CSS,value);
}

static int pmc_clk_mck_set_parent(struct clk_hw *hw, uint8_t index)
{
	struct pmc_clk_mck  *clk_data = to_pmc_clk_mck(hw);


	regmap_update_bits(clk_data->regmap, PMC_MCKR,
			__FIELD_MASK(PMC_MCKR,CSS),
			__FIELD_VALUE_SET(PMC_MCKR,CSS,index));

	return 0;
}



static const struct clk_ops pmc_clk_mck_ops = {
	.recalc_rate = pmc_clk_mck_recalc_rate,
	.round_rate = pmc_clk_mck_round_rate,
	.set_rate = pmc_clk_mck_set_rate,
	.set_parent = pmc_clk_mck_set_parent,
	.get_parent = pmc_clk_mck_get_parent,
};



static void __init pmc_clk_mck_init(struct device_node *np)
{
	struct clk_init_data init;
	struct pmc_clk_plla_div *clk_data;
	struct regmap *regmap;
	const char *parent_names[4];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(!clk_data){
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if(IS_ERR(regmap)){
		kfree(clk_data);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &pmc_clk_mck_ops;



	clk_data->hw.init = &init;
	clk_data->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);


}
CLK_OF_DECLARE(at91sam9_pmc_clk_mck,"atmel,at91sam9-clk-mck",pmc_clk_mck_init);

static unsigned long pmc_clk_plla_div_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct pmc_clk_plla_div *clk_data  = to_pmc_clk_plla_div(hw);
	uint32_t value;

	regmap_read(clk_data->regmap,PMC_MCKR,&value);

	return parent_rate/(1<<__FIELD_VALUE_GET(PMC_MCKR,PLLADIV2,value));
}
static long pmc_clk_plla_div_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	unsigned long div;

	if(rate > *parent_rate){
		return *parent_rate;
	}

	div = *parent_rate/2;

	if(rate < div){
		return div;
	}

	if(*parent_rate - rate > rate - div){
		return div;
	}

	return  *parent_rate;
}
static int pmc_clk_plla_div_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct pmc_clk_plla_div *clk_data  = to_pmc_clk_plla_div(hw);
	unsigned long div;

	if(parent_rate != rate && parent_rate/2 != rate){
		return -EINVAL;
	}

	div = parent_rate==rate?0:1;

	regmap_update_bits(clk_data->regmap, PMC_MCKR,
			__FIELD_MASK(PMC_MCKR,PLLADIV2),
			__FIELD_VALUE_SET(PMC_MCKR,PLLADIV2,div));

	return 0;
}

static const struct clk_ops pmc_clk_plla_div_ops = {
	.recalc_rate = pmc_clk_plla_div_recalc_rate,
	.round_rate = pmc_clk_plla_div_round_rate,
	.set_rate = pmc_clk_plla_div_set_rate,
};

static void __init pmc_clk_plla_div_init(struct device_node *np)
{
	struct clk_init_data init;
	struct pmc_clk_plla_div *clk_data;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(!clk_data){
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if(IS_ERR(regmap)){
		kfree(clk_data);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;
	init.ops = &pmc_clk_plla_div_ops;



	clk_data->hw.init = &init;
	clk_data->regmap = regmap;

	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);


}
CLK_OF_DECLARE(at91sam9_pmc_clk_plla_div,"atmel,at91sam9-clk-plla-div",pmc_clk_plla_div_init);

static int pmc_clk_plla_prepare(struct clk_hw *hw)
{
	struct pmc_clk_plla *clk_data = to_pmc_clk_plla(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,CKGR_PLLAR,&value);

	if(__FIELD_VALUE_GET(CKGR_PLLAR,MULA,value) == clk_data->mul &&
			__FIELD_VALUE_GET(CKGR_PLLAR,DIVA,value) == clk_data->div){
		return 0;
	}


	regmap_update_bits(clk_data->regmap, CKGR_PLLAR,
			__FIELD_MASK(CKGR_PLLAR,MULA)|
			__FIELD_MASK(CKGR_PLLAR,DIVA)|
			__FIELD_MASK(CKGR_PLLAR,OUTA),
			__FIELD_VALUE_FIXED(CKGR_PLLAR,ONE,VALID)|
			__FIELD_VALUE_FIXED(CKGR_PLLAR,PLLACOUNT,MAX)|
			__FIELD_VALUE_SET(CKGR_PLLAR,MULA,clk_data->mul)|
			__FIELD_VALUE_SET(CKGR_PLLAR,DIVA,clk_data->div)|
			__FIELD_VALUE_SET(CKGR_PLLAR,OUTA,clk_data->outa));

	regmap_update_bits(clk_data->regmap, PMC_PLLICPR,
			__FIELD_MASK(PMC_PLLICPR,ICPLLA),
			__FIELD_VALUE_SET(PMC_PLLICPR,ICPLLA,clk_data->icplla));

	do {
		regmap_read(clk_data->regmap,PMC_SR,&value);

	}while(!(value & PMC_SR_LOCKA));

	return 0;
}
static void pmc_clk_plla_unprepare(struct clk_hw *hw)
{
	struct pmc_clk_plla *clk_data = to_pmc_clk_plla(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,CKGR_PLLAR,&value);

	if(__FIELD_VALUE_GET(CKGR_PLLAR,MULA,value) == 0){
		return ;
	}

	value &= ~__FIELD_MASK(CKGR_PLLAR,MULA);
	value |= __FIELD_VALUE_FIXED(CKGR_PLLAR,ONE,VALID)|
			__FIELD_VALUE_SET(CKGR_PLLAR,MULA,0);

	regmap_write(clk_data->regmap,CKGR_PLLAR,value);

}
static int pmc_clk_plla_is_prepared(struct clk_hw *hw)
{
	struct pmc_clk_plla *clk_data = to_pmc_clk_plla(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,PMC_SR,&value);

	return value & PMC_SR_LOCKA;
}
static void pmc_clk_plla_get_best_div_mul(unsigned long rate,unsigned long parent_rate,
		int *best_div,int *best_mul)
{
	int mul;
	int div;
	unsigned long new_rate;
	unsigned long diff;
	unsigned long best_diff = ~0;

	for(mul = CKGR_PLLAR_MULA_MIN;mul <= CKGR_PLLAR_MULA_MAX;mul++){
		for(div = CKGR_PLLAR_DIVA_MIN;div <= CKGR_PLLAR_DIVA_MAX;div++){
			new_rate = DIV_ROUND_UP(parent_rate*(mul+1),div);
			if(new_rate < PLLA_MIN_FREQ ||
					new_rate > PLLA_MAX_FREQ){
				continue;
			}

			diff = abs(new_rate - rate);
			if(diff >= best_diff){
				continue;
			}

			*best_div = div;
			*best_mul = mul;
			best_diff = diff;
		}
	}


}

static unsigned long pmc_clk_plla_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct pmc_clk_plla *clk_data = to_pmc_clk_plla(hw);

	return DIV_ROUND_UP(parent_rate*(clk_data->mul+1),clk_data->div);
}



static long pmc_clk_plla_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	int div,mul;

	if(rate < PLLA_MIN_FREQ){
		rate = PLLA_MIN_FREQ;
	}

	if(rate > PLLA_MAX_FREQ){
		rate = PLLA_MAX_FREQ;
	}

	pmc_clk_plla_get_best_div_mul(rate,*parent_rate,&div,&mul);


	return DIV_ROUND_UP(*parent_rate*(mul+1),div);
}

static int pmc_clk_plla_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct pmc_clk_plla *clk_data = to_pmc_clk_plla(hw);
	int div,mul;
	int i;

	pmc_clk_plla_get_best_div_mul(rate,parent_rate,&div,&mul);

	for(i = 0;i < ARRAY_SIZE(pll_params);i++){
		if(pll_params[i].min <= rate &&
				rate <= pll_params[i].max){
			break;
		}
	}


	clk_data->div = (uint8_t)div;
	clk_data->mul = (uint8_t)mul;
	clk_data->outa = pll_params[i].outa;
	clk_data->icplla = pll_params[i].icplla;



	return 0;
}

static const struct clk_ops pmc_clk_plla_ops = {
	.prepare = pmc_clk_plla_prepare,
	.unprepare = pmc_clk_plla_unprepare,
	.is_prepared = pmc_clk_plla_is_prepared,
	.recalc_rate = pmc_clk_plla_recalc_rate,
	.round_rate = pmc_clk_plla_round_rate,
	.set_rate = pmc_clk_plla_set_rate,
};
static void __init pmc_clk_plla_init(struct device_node *np)
{
	struct clk_init_data init;
	struct pmc_clk_plla *clk_data;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	uint32_t pllar;
	uint32_t pllicpr;
	int num_parents;
	int ret;


	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(!clk_data){
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if(IS_ERR(regmap)){
		kfree(clk_data);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_GATE;
	init.ops = &pmc_clk_plla_ops;


	regmap_read(regmap,CKGR_PLLAR,&pllar);
	regmap_read(regmap,PMC_PLLICPR,&pllicpr);


	clk_data->hw.init = &init;
	clk_data->regmap = regmap;
	clk_data->mul = __FIELD_VALUE_GET(CKGR_PLLAR,MULA,pllar);
	clk_data->div = __FIELD_VALUE_GET(CKGR_PLLAR,DIVA,pllar);
	clk_data->outa = __FIELD_VALUE_GET(CKGR_PLLAR,OUTA,pllar);
	clk_data->icplla = __FIELD_VALUE_GET(PMC_PLLICPR,ICPLLA,pllicpr);

	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);
}
CLK_OF_DECLARE(at91sam9_pmc_clk_plla,"atmel,at91sam9-clk-plla",pmc_clk_plla_init);

static int pmc_clk_main_rc_osc_prepare(struct clk_hw *hw)
{
	struct pmc_clk_main_rc_osc *clk_data = to_pmc_clk_main_rc_osc(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,CKGR_MOR,&value) ;
	if(__FIELD_IS_SET(CKGR_MOR,MOSCRCEN,ENABLE,value)){
		return 0;
	}

	value &= ~(__FIELD_MASK(CKGR_MOR,MOSCRCEN) |
			__FIELD_MASK(CKGR_MOR,KEY));
	value |= __FIELD_VALUE_FIXED(CKGR_MOR,MOSCRCEN,ENABLE) |
			__FIELD_VALUE_FIXED(CKGR_MOR,KEY,PASSWD);

	regmap_write(clk_data->regmap,CKGR_MOR,value);

	do {
		cpu_relax();
		regmap_read(clk_data->regmap,PMC_SR,&value);
	} while(!(value & PMC_SR_MOSCRCS));


	return 0;

}
static void pmc_clk_main_rc_osc_unprepare(struct clk_hw *hw)
{
	struct pmc_clk_main_rc_osc *clk_data = to_pmc_clk_main_rc_osc(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,CKGR_MOR,&value);
	if(__FIELD_IS_SET(CKGR_MOR,MOSCRCEN,DISABLE,value)){
		return ;
	}

	value &= ~(__FIELD_MASK(CKGR_MOR,MOSCRCEN) |
			__FIELD_MASK(CKGR_MOR,KEY));
	value |= __FIELD_VALUE_FIXED(CKGR_MOR,MOSCRCEN,DISABLE) |
			__FIELD_VALUE_FIXED(CKGR_MOR,KEY,PASSWD);


	regmap_write(clk_data->regmap,CKGR_MOR,value);

}
static int pmc_clk_main_rc_osc_is_prepared(struct clk_hw *hw)
{
	struct pmc_clk_main_rc_osc *clk_data = to_pmc_clk_main_rc_osc(hw);
	uint32_t mor = 0;
	uint32_t status = 0;

	regmap_read(clk_data->regmap,CKGR_MOR,&mor) ;
	regmap_read(clk_data->regmap,PMC_SR,&status) ;

	return __FIELD_IS_SET(CKGR_MOR,MOSCRCEN,ENABLE,mor) &&
			(status & PMC_SR_MOSCRCS);

}

static unsigned long pmc_clk_main_rc_osc_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct pmc_clk_main_rc_osc *clk_data = to_pmc_clk_main_rc_osc(hw);

	return clk_data->frequency;
}


static unsigned long pmc_clk_main_rc_osc_recalc_accuracy(struct clk_hw *hw,
						     unsigned long parent_acc)
{
	struct pmc_clk_main_rc_osc *clk_data = to_pmc_clk_main_rc_osc(hw);

	return clk_data->accuracy;
}



static const struct clk_ops pmc_clk_main_rc_osc_ops = {
	.prepare = pmc_clk_main_rc_osc_prepare,
	.unprepare = pmc_clk_main_rc_osc_unprepare,
	.is_prepared = pmc_clk_main_rc_osc_is_prepared,
	.recalc_rate = pmc_clk_main_rc_osc_recalc_rate,
	.recalc_accuracy = pmc_clk_main_rc_osc_recalc_accuracy,
};

static void __init pmc_clk_main_rc_osc_init(struct device_node *np)
{
	struct clk_init_data init;
	struct pmc_clk_main_rc_osc *clk_data;
	struct regmap *regmap;
	const char *name = np->name;
	uint32_t frequency,accuracy;
	int ret;


	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(!clk_data){
		return ;
	}

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if(IS_ERR(regmap)){
		kfree(clk_data);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);
	of_property_read_u32(np,"clock-frequency",&frequency);
	of_property_read_u32(np,"clock-accuracy",&accuracy);


	init.name = name;
	init.num_parents = 0;
	init.flags = CLK_IGNORE_UNUSED;
	init.ops = &pmc_clk_main_rc_osc_ops;


	clk_data->hw.init = &init;
	clk_data->frequency = frequency;
	clk_data->accuracy = accuracy;
	clk_data->regmap = regmap;
	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);


}
CLK_OF_DECLARE(at91sam9_pmc_clk_main_rc_osc,"atmel,at91sam9-clk-main-rc-osc",pmc_clk_main_rc_osc_init);

static int pmc_clk_main_osc_prepare(struct clk_hw *hw)
{
	struct pmc_clk_main_osc *clk_data = to_pmc_clk_main_osc(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,CKGR_MOR,&value) ;
	if(__FIELD_IS_SET(CKGR_MOR,MOSCXTBY,ENABLE,value)||
			__FIELD_IS_SET(CKGR_MOR,MOSCXTEN,ENABLE,value)){
		return 0;
	}

	value &= ~(__FIELD_MASK(CKGR_MOR,MOSCXTEN) |
			__FIELD_MASK(CKGR_MOR,KEY));
	value |= __FIELD_VALUE_FIXED(CKGR_MOR,MOSCXTEN,ENABLE) |
			__FIELD_VALUE_FIXED(CKGR_MOR,KEY,PASSWD);

	regmap_write(clk_data->regmap,CKGR_MOR,value);

	do {
		cpu_relax();
		regmap_read(clk_data->regmap,PMC_SR,&value);
	} while(!(value & PMC_SR_MOSCXTS));


	return 0;
}
static void pmc_clk_main_osc_unprepare(struct clk_hw *hw)
{
	struct pmc_clk_main_osc *clk_data = to_pmc_clk_main_osc(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,CKGR_MOR,&value) ;
	if(__FIELD_IS_SET(CKGR_MOR,MOSCXTBY,ENABLE,value)||
			__FIELD_IS_SET(CKGR_MOR,MOSCXTEN,DISABLE,value)){
		return ;
	}

	value &= ~(__FIELD_MASK(CKGR_MOR,MOSCXTEN) |
			__FIELD_MASK(CKGR_MOR,KEY));
	value |= __FIELD_VALUE_FIXED(CKGR_MOR,MOSCXTEN,DISABLE) |
			__FIELD_VALUE_FIXED(CKGR_MOR,KEY,PASSWD);

	regmap_write(clk_data->regmap,CKGR_MOR,value);
}
static int pmc_clk_main_osc_is_prepared(struct clk_hw *hw)
{
	struct pmc_clk_main_osc *clk_data = to_pmc_clk_main_osc(hw);
	uint32_t mor = 0;
	uint32_t status = 0;

	regmap_read(clk_data->regmap,CKGR_MOR,&mor) ;
	regmap_read(clk_data->regmap,PMC_SR,&status) ;

	return __FIELD_IS_SET(CKGR_MOR,MOSCXTEN,ENABLE,mor) &&
			(status & PMC_SR_MOSCXTS);

}

static const struct clk_ops pmc_clk_main_osc_ops = {
	.prepare = pmc_clk_main_osc_prepare,
	.unprepare = pmc_clk_main_osc_unprepare,
	.is_prepared = pmc_clk_main_osc_is_prepared,
};

static void __init pmc_clk_main_osc_init(struct device_node *np)
{
	struct clk_init_data init;
	struct pmc_clk_main_osc *clk_data;
	struct regmap *regmap;
	const char *parent_names[1];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(!clk_data){
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);
	regmap = syscon_node_to_regmap(of_get_parent(np));
	if(IS_ERR(regmap)){
		kfree(clk_data);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_IGNORE_UNUSED;
	init.ops = &pmc_clk_main_osc_ops;


	clk_data->hw.init = &init;
	clk_data->regmap = regmap;
	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);
}
CLK_OF_DECLARE(at91sam9_pmc_clk_main_osc,"atmel,at91sam9-clk-main-osc",pmc_clk_main_osc_init);



static unsigned long clk_main_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct pmc_clk_main *clk_data = to_pmc_clk_main(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,PMC_SR,&value);
	if(!(value & PMC_SR_OSCSELS)){
		return parent_rate;
	}

	do {
		cpu_relax();
		regmap_read(clk_data->regmap,CKGR_MCFR,&value);
	} while(!__FIELD_IS_SET(CKGR_MCFR,MAINFRDY,READY,value));


	return (__FIELD_VALUE_GET(CKGR_MCFR,MAINF,value) * SLOW_CLOCK_FREQ)/MAINF_DIV;

}

static uint8_t clk_main_get_parent(struct clk_hw *hw)
{
	struct pmc_clk_main *clk_data = to_pmc_clk_main(hw);
	uint32_t value = 0;

	regmap_read(clk_data->regmap,CKGR_MOR,&value);

	return __FIELD_VALUE_GET(CKGR_MOR,MOSCSEL,value);
}

static int clk_main_set_parent(struct clk_hw *hw, uint8_t index)
{
	struct pmc_clk_main *clk_data = to_pmc_clk_main(hw);
	uint32_t value = 0;

	if(index > 1){
		return -EINVAL;
	}

	regmap_read(clk_data->regmap,CKGR_MOR,&value);

	if(__FIELD_VALUE_GET(CKGR_MOR,MOSCSEL,value) == index){
		return 0;
	}

	value &= ~(__FIELD_MASK(CKGR_MOR,MOSCSEL) |
			__FIELD_MASK(CKGR_MOR,KEY));
	value |= __FIELD_VALUE_SET(CKGR_MOR,MOSCSEL,index) |
			__FIELD_VALUE_FIXED(CKGR_MOR,KEY,PASSWD);

	regmap_write(clk_data->regmap,CKGR_MOR,value);

	do {
		regmap_read(clk_data->regmap,PMC_SR,&value);
	} while(!(value & PMC_SR_MOSCSELS));

	return 0;
}

static const struct clk_ops pmc_clk_main_ops = {
	.recalc_rate = clk_main_recalc_rate,
	.get_parent = clk_main_get_parent,
	.set_parent = clk_main_set_parent,
};

static void __init pmc_clk_main_init(struct device_node *np)
{
	struct clk_init_data init;
	struct pmc_clk_main *clk_data;
	struct regmap *regmap;
	const char *parent_names[2];
	const char *name = np->name;
	int num_parents;
	int ret;


	clk_data = kzalloc(sizeof(*clk_data),GFP_KERNEL);
	if(!clk_data){
		return ;
	}


	num_parents = of_clk_get_parent_count(np);
	if (num_parents != ARRAY_SIZE(parent_names)){
		return;
	}
	of_clk_parent_fill(np, parent_names, num_parents);
	regmap = syscon_node_to_regmap(of_get_parent(np));
	if(IS_ERR(regmap)){
		kfree(clk_data);
		return ;
	}

	of_property_read_string(np,"clock-output-names",&name);


	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_PARENT_GATE;
	init.ops = &pmc_clk_main_ops;


	clk_data->hw.init = &init;
	clk_data->regmap = regmap;
	ret = clk_hw_register(NULL,&clk_data->hw);
	if(ret){
		kfree(clk_data);
		return ;
	}


	of_clk_add_hw_provider(np, of_clk_hw_simple_get, &clk_data->hw);
}
CLK_OF_DECLARE(at91sam9_pmc_clk_main,"atmel,at91sam9-clk-main",pmc_clk_main_init);

