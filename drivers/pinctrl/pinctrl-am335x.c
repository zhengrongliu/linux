/*
 * pinctrl-am335x.c
 *
 *  Created on: Mar 17, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include "core.h"
#include "pinmux.h"
#include "pinconf.h"

#define DRIVER_NAME "am335x-pinctrl"

/*
 * CM_CONF_MODULE_PIN register
 */
#define CM_CONF_MODULE_PIN_MODE_SHIFT		0
#define CM_CONF_MODULE_PIN_MODE_WIDTH		3
#define CM_CONF_MODULE_PIN_PUDEN_SHIFT		3
#define CM_CONF_MODULE_PIN_PUDEN_WIDTH		1
#define CM_CONF_MODULE_PIN_PUTYPE_SHIFT		4
#define CM_CONF_MODULE_PIN_PUTYPE_WIDTH		1
#define CM_CONF_MODULE_PIN_RX_ENABLE_SHIFT	5
#define CM_CONF_MODULE_PIN_RX_ENABLE_WIDTH	1
#define CM_CONF_MODULE_PIN_SLEW_CTRL_SHIFT	6
#define CM_CONF_MODULE_PIN_SLEW_CTRL_WIDTH	1


#define AM335X_PIN_DESC(n,s,a) {(n),(s),(void*)(a)}

#define AM335X_FUNC_DESC(n)	{#n,n##_group_names,ARRAY_SIZE(n##_group_names)}
//#define AM335X_FUNC_GROUP(n,x)	static const char * n##_group_names[] = x
#define AM335X_FUNC_GROUP(n,...)	static const char * n##_group_names[] = {__VA_ARGS__}

struct am335x_pinctrl{
	struct pinctrl_dev *pctrl;
	struct regmap *regmap;

};

struct am335x_pinctrl_function_desc{
	const char *name;
	const char ** group_names;
	int ngroups;
};

enum am335x_pin_config_param {
	PIN_CONFIG_AM335X_PIN_MODE = PIN_CONFIG_END+1,
};

static const struct pinconf_generic_params am335x_pinctrl_params[] = {
	{"ti,pin-mode",PIN_CONFIG_AM335X_PIN_MODE,0}
};


AM335X_FUNC_GROUP(uart0_txd,"uart0_txd");
AM335X_FUNC_GROUP(uart0_rxd,"uart0_rxd");
AM335X_FUNC_GROUP(i2c0_scl,"i2c0_scl");
AM335X_FUNC_GROUP(i2c0_sda,"i2c0_sda");

static const struct am335x_pinctrl_function_desc am335x_functions [] = {
	AM335X_FUNC_DESC(uart0_txd),
	AM335X_FUNC_DESC(uart0_rxd),
	AM335X_FUNC_DESC(i2c0_scl),
	AM335X_FUNC_DESC(i2c0_sda),
};

static const struct pinctrl_pin_desc am335x_pins[] = {
	AM335X_PIN_DESC(0,"uart0_txd",0x974),
	AM335X_PIN_DESC(1,"uart0_rxd",0x970),
	AM335X_PIN_DESC(2,"i2c0_scl",0x98c),
	AM335X_PIN_DESC(3,"i2c0_sda",0x988),
};



static int am335x_pinctrl_pin_config_set(struct pinctrl_dev *pctldev,
			       unsigned pin,
			       unsigned long *configs,
			       unsigned num_configs)
{
	struct am335x_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct regmap *regmap = pctrl->regmap;
	uint32_t reg = (uint32_t)am335x_pins[pin].drv_data;
	uint32_t value = 0;
	uint32_t pull_disable = 1;
	uint32_t param;
	uint32_t argument;
	int i;


        for (i = 0; i < num_configs; i++) {
        	param = pinconf_to_config_param(configs[i]);
        	argument = pinconf_to_config_argument(configs[i]);

        	switch(param){
        	case PIN_CONFIG_AM335X_PIN_MODE:
        		value |= argument;
        		break;
        	case PIN_CONFIG_INPUT_ENABLE:
        		value |= 1 << CM_CONF_MODULE_PIN_RX_ENABLE_SHIFT;
        		break;
        	case PIN_CONFIG_BIAS_DISABLE:
        		pull_disable = 1;
        		break;
        	case PIN_CONFIG_BIAS_PULL_DOWN:
        		value |= 0 << CM_CONF_MODULE_PIN_PUTYPE_SHIFT;
        		pull_disable = 0;
        		break;
        	case PIN_CONFIG_BIAS_PULL_UP:
        		value |= 1 << CM_CONF_MODULE_PIN_PUTYPE_SHIFT;
        		pull_disable = 0;
        		break;
        	default:
        		break;
        	}

        }

        value |= pull_disable << CM_CONF_MODULE_PIN_PUDEN_SHIFT;

	regmap_write(regmap,reg,value);

	return 0;
}




static int am335x_pinctrl_set_mux(struct pinctrl_dev *pctldev, unsigned func_selector,
			unsigned group_selector)
{
	return 0;
}


static int am335x_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
			       struct device_node *np_config,
			       struct pinctrl_map **map, unsigned *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev,np_config,
			map,num_maps,PIN_MAP_TYPE_CONFIGS_PIN);
}


const struct pinconf_ops am335x_pinctrl_conf_ops = {
	.pin_config_set		= am335x_pinctrl_pin_config_set,
};
const struct pinmux_ops am335x_pinctrl_mux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= am335x_pinctrl_set_mux,
};

const struct pinctrl_ops am335x_pinctrl_ops = {
	.get_groups_count 	= pinctrl_generic_get_group_count,
	.get_group_name 	= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= am335x_pinctrl_dt_node_to_map,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

static struct pinctrl_desc am335x_pinctrl_desc = {
	.name = "am335x-pinctrl",
	.pins = am335x_pins,
	.npins = ARRAY_SIZE(am335x_pins),
	.pctlops = &am335x_pinctrl_ops,
	.pmxops = &am335x_pinctrl_mux_ops,
	.confops = &am335x_pinctrl_conf_ops,
	.custom_params = am335x_pinctrl_params,
	.num_custom_params = ARRAY_SIZE(am335x_pinctrl_params),
	.owner = THIS_MODULE,
};

int am335x_pinctrl_probe(struct platform_device *pdev)
{
	struct am335x_pinctrl * pctrl;
	int i;
	int ret;


	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if(pctrl == NULL){
		dev_err(&pdev->dev,"no memory for alloc pinctrl node");
		return -ENOMEM;
	}

	pctrl->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,"syscon");
	if (IS_ERR(pctrl->regmap)) {
		dev_err(&pdev->dev, "unable to get syscon\n");
		return PTR_ERR(pctrl->regmap);
	}


	ret= devm_pinctrl_register_and_init(&pdev->dev,
			&am335x_pinctrl_desc, pctrl,&pctrl->pctrl);
	if(ret){
		dev_err(&pdev->dev,"register pinctrl failed");
		return ret;
	}

	for(i = 0;i < ARRAY_SIZE(am335x_pins);i++){
		pinctrl_generic_add_group(pctrl->pctrl,
				am335x_pins[i].name,(int*)&am335x_pins[i].number,1,NULL);
	}

	for(i = 0;i < ARRAY_SIZE(am335x_functions);i++){
		pinmux_generic_add_function(pctrl->pctrl,
				am335x_functions[i].name,
				am335x_functions[i].group_names,
				am335x_functions[i].ngroups,NULL);
	}

	ret = pinctrl_enable(pctrl->pctrl);
	if(ret){
		dev_err(&pdev->dev,"enable pinctrl failed");
		return ret;
	}

	platform_set_drvdata(pdev, pctrl);

	return 0;
}

static const struct of_device_id am335x_pinctrl_of_match[] = {
	{.compatible = "ti,am335x-pinctrl",},
	{}
};

static struct platform_driver am335x_pinctrl_driver = {
	.probe = am335x_pinctrl_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(am335x_pinctrl_of_match),
	},
};

static int __init am335x_pinctrl_init(void)
{
	return platform_driver_register(&am335x_pinctrl_driver);
}
arch_initcall(am335x_pinctrl_init);


