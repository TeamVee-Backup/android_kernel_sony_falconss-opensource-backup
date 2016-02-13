/*
 * Copyright (C) 2010 MEMSIC, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <linux/input-polldev.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>
//#include <linux/i2c/mmc3416x.h>
#include <linux/mmc3416x.h>

/*++ Huize - 20121107 Add for getting device information ++*/
#include <linux/regulator/consumer.h>
#include <linux/sensors_mctl.h>
/*-- Huize - 20121107 Add for getting device information --*/

#define DEBUG			0
#define MAX_FAILURE_COUNT	3
#define READMD			0

#define MMC3416X_DELAY_TM	10	/* ms */
#define MMC3416X_DELAY_SET	75	/* ms */
#define MMC3416X_DELAY_RESET     75     /* ms */

#define MMC3416X_RETRY_COUNT	3
#define MMC3416X_SET_INTV	250

#define MMC3416X_DEV_NAME	"mmc3416x"

static u32 read_idx = 0;
struct class *mag_class;

static struct i2c_client *this_client;

static struct input_polled_dev *ipdev;
static struct mutex lock;
struct mmc3416x_data {
	struct i2c_client	*i2c;
	struct input_dev	*input;
	struct device		*class_dev;
	struct class		*compass;
	struct delayed_work	work;

	wait_queue_head_t	drdy_wq;
	wait_queue_head_t	open_wq;

	struct mutex sensor_mutex;
	struct regulator* vdd;
	struct regulator* vdd_i2c;
	
};

static struct mmc3416x_data *s_mmc;
//static short ecompass_delay = 0;
static bool mmc3416x_setActive(bool enable);
static bool mmc3416x_setDelay(s64 delay);
static int mmc3xxx_i2c_tx_data(char *buf, int len);

static struct device_infor infor = {
	.name		= "MMC3416 Magnetic Field Sensor",
	.vendor		= "ATM ELECTRONIC CORP",
	.maxRange	= 49152,
	.resolution = 0,
	.power		= 280,// ua
	.minDelay	= 10000,
};

static sensor_device MAGNETIC = {
	.name = "magnetic",
	.enable = true,
	
	.debugLevel = 5,
	.setActive = mmc3416x_setActive,
	.setDelay = mmc3416x_setDelay,
	.calibrate = NULL,
	.infor = &infor,
};
static bool mmc3416x_setPower(struct device* dev, bool enable)
{
	int rc = 0;
	
	if(!s_mmc) return false;
	if(enable == false) goto disable;

	rc = min(regulator_set_optimum_mode(s_mmc->vdd, 15000), 
			regulator_set_optimum_mode(s_mmc->vdd_i2c, 15000));
	if (rc < 0) {
		pr_err("Regulator vcc_ana set_opt failed rc = %d\n", rc);
		return false;
	}

	rc = regulator_enable(s_mmc->vdd);
	rc |= regulator_enable(s_mmc->vdd_i2c);
	if(rc){
		pr_err("Regulator vcc_ana enable failed rc=%d\n", rc);
		goto error_reg_en_vcc_ana;
	}

	usleep(50000);
	return true;

error_reg_en_vcc_ana:
	regulator_set_optimum_mode(s_mmc->vdd, 0);
	regulator_set_optimum_mode(s_mmc->vdd_i2c, 0);
	return false;

disable:
	regulator_set_optimum_mode(s_mmc->vdd, 0);
	regulator_set_optimum_mode(s_mmc->vdd_i2c, 0);
	if(s_mmc->vdd)
		regulator_disable(s_mmc->vdd);
	if(s_mmc->vdd_i2c)
	regulator_disable(s_mmc->vdd);
	s_mmc->vdd= s_mmc->vdd_i2c = NULL;
	return true;
}

static bool mmc3416x_powerSetting(struct device* dev, bool enable)
{
	int rc = 0;
	if(enable == false) goto disable;

	s_mmc->vdd = regulator_get(dev, "vdd_ana");
	if(IS_ERR(s_mmc->vdd)){
		pr_err("Regulator get failed vdd_ana rc = %ld\n", PTR_ERR(s_mmc->vdd));
		s_mmc->vdd = s_mmc->vdd_i2c = NULL;
		return false;
	}

	s_mmc->vdd_i2c = regulator_get(dev, "vcc_i2c");
	if(IS_ERR(s_mmc->vdd_i2c)){
		pr_err("Regulator get failed vcc_i2c rc = %ld\n", PTR_ERR(s_mmc->vdd_i2c));
		regulator_put(s_mmc->vdd);
		s_mmc->vdd = s_mmc->vdd_i2c = NULL;
		return false;
	}

	rc = min(regulator_count_voltages(s_mmc->vdd), 
		regulator_count_voltages(s_mmc->vdd_i2c));
	if(rc < 0){
		pr_err("regulator count_vtg failed rc = %d\n", rc);
		goto error_count_vtg;
	}
	
	rc = regulator_set_voltage(s_mmc->vdd, 2850000, 2850000);
	rc |= regulator_set_voltage(s_mmc->vdd_i2c, 1800000, 1800000);
	if(rc){
		pr_err("regulator set_vtg failed rc = %d\n", rc);
		goto error_set_vtg;
	}
	return true;

error_set_vtg:
error_count_vtg:
	regulator_put(s_mmc->vdd_i2c);
	regulator_put(s_mmc->vdd);
	s_mmc->vdd = s_mmc->vdd_i2c = NULL;
	return false;
disable:
	if(!s_mmc->vdd_i2c){
		if(regulator_count_voltages(s_mmc->vdd_i2c) > 0){
			regulator_set_voltage(s_mmc->vdd_i2c, 0, 1800000);
		}
		regulator_put(s_mmc->vdd_i2c);
	}
	if(!s_mmc->vdd){
		if(regulator_count_voltages(s_mmc->vdd) > 0){
			regulator_set_voltage(s_mmc->vdd, 0, 3300000);
		}
		regulator_put(s_mmc->vdd);;
	}
	s_mmc->vdd = s_mmc->vdd_i2c = NULL;
	return true;
}

static bool mmc3416x_enable(void)
{
      Magnetic* mag = i2c_get_clientdata(this_client);
       unsigned char data[2] = {0};
	
	

     sDump_debug(MAGNETIC.debugLevel, "%s", __func__);

       data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_REFILL;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_SET);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_RESET;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = 0;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_SET);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_REFILL;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_RESET);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_SET;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = 0;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);

	data[0] = MMC3416X_REG_BITS;
	data[1] = MMC3416X_BITS_SLOW_16;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_TM);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_TM;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	
     queue_delayed_work(Magnetic_WorkQueue, &mag->dw, MagneticSleepTime); 

return true;
}

static bool mmc3416x_disable(void)
{
     Magnetic* data = i2c_get_clientdata(this_client);

    sDump_debug(MAGNETIC.debugLevel,  "%s", __func__);



	
    cancel_delayed_work_sync(&data->dw);
    flush_workqueue(Magnetic_WorkQueue);
	
return true;
}

static bool mmc3416x_setActive(bool enable)
{
sDump_debug(MAGNETIC.debugLevel,"%s enable=%d",__func__,enable);

return (enable) ? mmc3416x_enable() : mmc3416x_disable();


}

static bool mmc3416x_setDelay(s64 delay)
{

//Magnetic* data = i2c_get_clientdata(this_client);
	
	sDump_debug(MAGNETIC.debugLevel, "%s ++\n", __func__);

	delay=10;

	sDump_debug(MAGNETIC.debugLevel, "%s --\n", __func__);
	return true;
}


static DEFINE_MUTEX(ecompass_lock);

static int mmc3xxx_i2c_rx_data(char *buf, int len)
{
	uint8_t i;
	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};

	for (i = 0; i < MMC3416X_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msgs, 2) >= 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MMC3416X_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MMC3416X_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int mmc3xxx_i2c_tx_data(char *buf, int len)
{
	uint8_t i;
	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= len,
			.buf	= buf,
		}
	};
	
	for (i = 0; i < MMC3416X_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msg, 1) >= 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MMC3416X_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MMC3416X_RETRY_COUNT);
		return -EIO;
	}
	return 0;
}

static int mmc3416x_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int mmc3416x_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long mmc3416x_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *pa = (void __user *)arg;
	int __user *pa_i = (void __user *)arg;
	unsigned char data[16] = {0};
	int vec[3] = {0};
	int reg;
	short flag;
	//short delay;

	mutex_lock(&ecompass_lock);
	switch (cmd) {
	case MMC3416X_IOC_DIAG:
		if (get_user(reg, pa_i))
			return -EFAULT;
		data[0] = (unsigned char)((0xff)&reg);
		if (mmc3xxx_i2c_rx_data(data, 1) < 0) {
			return -EFAULT;
		}
		if (put_user(data[0], pa_i))
			return -EFAULT;
		break;
	case MMC3416X_IOC_TM:
		data[0] = MMC3416X_REG_CTRL;
		data[1] = MMC3416X_CTRL_TM;
		if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		/* wait TM done for coming data read */
		msleep(MMC3416X_DELAY_TM);
		break;
	case MMC3416X_IOC_SET:
		data[0] = MMC3416X_REG_CTRL;
		data[1] = MMC3416X_CTRL_REFILL;
		if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		msleep(MMC3416X_DELAY_SET);
		data[0] = MMC3416X_REG_CTRL;
		data[1] = MMC3416X_CTRL_SET;
		if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		msleep(1);
		data[0] = MMC3416X_REG_CTRL;
		data[1] = 0;
		if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		msleep(MMC3416X_DELAY_SET);
		break;
	case MMC3416X_IOC_RESET:
		data[0] = MMC3416X_REG_CTRL;
		data[1] = MMC3416X_CTRL_REFILL;
		if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		msleep(MMC3416X_DELAY_RESET);
		data[0] = MMC3416X_REG_CTRL;
		data[1] = MMC3416X_CTRL_RESET;
		if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		msleep(1);
		data[0] = MMC3416X_REG_CTRL;
		data[1] = 0;
		if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		msleep(1);
		break;
	case MMC3416X_IOC_READ:
		data[0] = MMC3416X_REG_DATA;
		if (mmc3xxx_i2c_rx_data(data, 6) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		vec[0] = data[1] << 8 | data[0];
		vec[1] = data[3] << 8 | data[2];
		vec[2] = data[5] << 8 | data[4];
		vec[2] = 65536 - vec[2];	
	#if DEBUG
		printk("[X - %04x] [Y - %04x] [Z - %04x]\n", 
			vec[0], vec[1], vec[2]);
	#endif
		if (copy_to_user(pa, vec, sizeof(vec))) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		break;
	case MMC3416X_IOC_READXYZ:
		if (!(read_idx % MMC3416X_SET_INTV)) {
		    data[0] = MMC3416X_REG_CTRL;
		    data[1] = MMC3416X_CTRL_REFILL;
		    mmc3xxx_i2c_tx_data(data, 2);
		    msleep(MMC3416X_DELAY_RESET);
		    data[0] = MMC3416X_REG_CTRL;
		    data[1] = MMC3416X_CTRL_RESET;
		    mmc3xxx_i2c_tx_data(data, 2);
		    msleep(1);
		    data[0] = MMC3416X_REG_CTRL;
		    data[1] = 0;
		    mmc3xxx_i2c_tx_data(data, 2);
		    msleep(1);

	        data[0] = MMC3416X_REG_CTRL;
	        data[1] = MMC3416X_CTRL_REFILL;
	        mmc3xxx_i2c_tx_data(data, 2);
	        msleep(MMC3416X_DELAY_SET);
	        data[0] = MMC3416X_REG_CTRL;
	        data[1] = MMC3416X_CTRL_SET;
	        mmc3xxx_i2c_tx_data(data, 2);
	        msleep(1);
	        data[0] = MMC3416X_REG_CTRL;
	        data[1] = 0;
	        mmc3xxx_i2c_tx_data(data, 2);
	        msleep(1);
		}
		/* send TM cmd before read */
		data[0] = MMC3416X_REG_CTRL;
		data[1] = MMC3416X_CTRL_TM;
		/* not check return value here, assume it always OK */
		mmc3xxx_i2c_tx_data(data, 2);
		/* wait TM done for coming data read */
		msleep(MMC3416X_DELAY_TM);
#if READMD
		/* Read MD */
		data[0] = MMC3416X_REG_DS;
		if (mmc3xxx_i2c_rx_data(data, 1) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		while (!(data[0] & 0x01)) {
			msleep(1);
			/* Read MD again*/
			data[0] = MMC3416X_REG_DS;
			if (mmc3xxx_i2c_rx_data(data, 1) < 0) {
	                        mutex_unlock(&ecompass_lock);
				return -EFAULT;
                        }
			
			if (data[0] & 0x01) break;
			MD_times++;
			if (MD_times > 2) {
	                        mutex_unlock(&ecompass_lock);
		#if DEBUG
				printk("TM not work!!");
		#endif
				return -EFAULT;
			}
		}
#endif		
		/* read xyz raw data */
		read_idx++;
		data[0] = MMC3416X_REG_DATA;
		if (mmc3xxx_i2c_rx_data(data, 6) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
		vec[0] = data[1] << 8 | data[0];
		vec[1] = data[3] << 8 | data[2];
		vec[2] = data[5] << 8 | data[4];
		vec[2] = 65536 - vec[2];	
	#if DEBUG
		printk("[X - %04x] [Y - %04x] [Z - %04x]\n", 
			vec[0], vec[1], vec[2]);
	#endif
		if (copy_to_user(pa, vec, sizeof(vec))) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}

		break;
	case MMC3416X_IOC_ID:
		data[0] = MMC3416X_REG_PRODUCTID_0;
		if (mmc3xxx_i2c_rx_data(data, 1) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
                data[14] = data[0];
		data[0] = MMC3416X_REG_PRODUCTID_1;
		if (mmc3xxx_i2c_rx_data(data, 1) < 0) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
		}
                data[15] = data[0];
                flag = data[15] << 8 | data[14];
		if (copy_to_user(pa, &flag, sizeof(flag))) {
	                mutex_unlock(&ecompass_lock);
			return -EFAULT;
                }
                break;
	default:
		break;
	}
	mutex_unlock(&ecompass_lock);

	return 0;
}
#if 0
static ssize_t mmc3416x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "MMC3416X");
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(mmc3416x, S_IRUGO, mmc3416x_show, NULL);
#endif
static struct file_operations mmc3416x_fops = {
	.owner		= THIS_MODULE,
	.open		= mmc3416x_open,
	.release	= mmc3416x_release,
	.unlocked_ioctl = mmc3416x_ioctl,
};

static struct miscdevice mmc3416x_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MMC3416X_DEV_NAME,
	.fops = &mmc3416x_fops,
};
#if 0
static void mmc3416x_poll(struct input_polled_dev *ipdev)
{
	unsigned char data[16] = {0, 0, 0, 0, 0, 0, 0, 0,
				  0, 0, 0, 0, 0, 0, 0, 0};
	int vec[3] = {0, 0, 0};
	static int first = 1;
	mutex_lock(&lock);
	if (!first) {
		/* read xyz raw data */
		read_idx++;
		data[0] = MMC3416X_REG_DATA;
		if (mmc3xxx_i2c_rx_data(data, 6) < 0) {
			mutex_unlock(&ecompass_lock);
			return;
		}
		vec[0] = data[1] << 8 | data[0];
		vec[1] = data[3] << 8 | data[2];
		vec[2] = data[5] << 8 | data[4];
		vec[2] = 65536 - vec[2];	
#if DEBUG
		printk("[X - %04x] [Y - %04x] [Z - %04x]\n", 
			vec[0], vec[1], vec[2]);
#endif
		input_report_abs(ipdev->input, ABS_X, vec[0]);
		input_report_abs(ipdev->input, ABS_Y, vec[1]);
		input_report_abs(ipdev->input, ABS_Z, vec[2]);

		input_sync(ipdev->input);
	} else {
		first = 0;
	}

	/* send TM cmd before read */
	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_TM;
	/* not check return value here, assume it always OK */
	mmc3xxx_i2c_tx_data(data, 2);
	msleep(MMC3416X_DELAY_TM);
	mutex_unlock(&lock);
}


static struct input_polled_dev * mmc3416x_input_init(struct i2c_client *client)
{
	struct input_polled_dev *ipdev;
	int status;

	ipdev = input_allocate_polled_device();
	if (!ipdev) {
		dev_dbg(&client->dev, "error creating input device\n");
		return NULL;
	}
	ipdev->poll = mmc3416x_poll;
	ipdev->poll_interval = 20;       /* 50Hz */
	ipdev->private = client;
       
	ipdev->input->name = "Mmagnetometer";
	ipdev->input->phys = "mmc3416x/input0";
	ipdev->input->id.bustype = BUS_HOST;

	set_bit(EV_ABS, ipdev->input->evbit);

	input_set_abs_params(ipdev->input, ABS_X, -2047, 2047, 0, 0);
	input_set_abs_params(ipdev->input, ABS_Y, -2047, 2047, 0, 0);
	input_set_abs_params(ipdev->input, ABS_Z, -2047, 2047, 0, 0);

	input_set_capability(ipdev->input, EV_REL, REL_X);
	input_set_capability(ipdev->input, EV_REL, REL_Y);
	input_set_capability(ipdev->input, EV_REL, REL_Z);

	status = input_register_polled_device(ipdev);
	if (status) {
		dev_dbg(&client->dev,
			"error registering input device\n");
		input_free_polled_device(ipdev);
		return NULL;
	}
	return ipdev;
}

#endif
static void mmc3415x_work_func(struct work_struct *work)
{    
       Magnetic* mag = i2c_get_clientdata(this_client);
	   
       unsigned char data[6] = {0};
	int vec[3] = {0};


	sDump_debug(MAGNETIC.debugLevel, "%s --\n", __func__);

//Read
	data[0] = MMC3416X_REG_DATA;
		if (mmc3xxx_i2c_rx_data(data, 6) < 0) {
	                
		sDump_debug(MAGNETIC.debugLevel, "mmc3xxx_i2c_rx_data------Error\n");	
		}
		vec[0] = data[1] << 8 | data[0];
		vec[1] = data[3] << 8 | data[2];
		vec[2] = data[5] << 8 | data[4];
		vec[2] = 65536 - vec[2];	
	#if 1
		printk("[X - %04x] [Y - %04x] [Z - %04x]\n", 
			vec[0], vec[1], vec[2]);
	#endif
		
//-------------------------
	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_REFILL;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_SET);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_RESET;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = 0;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_SET);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_REFILL;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_RESET);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_SET;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = 0;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);

	data[0] = MMC3416X_REG_BITS;
	data[1] = MMC3416X_BITS_SLOW_16;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_TM);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_TM;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	
	queue_delayed_work(Magnetic_WorkQueue, &mag->dw, MagneticSleepTime); 
}

static int mmc3416x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	//unsigned char data[16] = {0};
	int res = 0;
	Magnetic* Sensor = NULL;
	struct input_dev* input_dev = NULL;
sDump_debug(MAGNETIC.debugLevel, "%s ++\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: functionality check failed\n", __FUNCTION__);
		res = -ENODEV;
		goto out;
	}
	this_client = client;

	res = misc_register(&mmc3416x_device);
	if (res) {
		pr_err("%s: mmc3416x_device register failed\n", __FUNCTION__);
		goto out;
	}
	/* Allocate memory for driver data */
	s_mmc = kzalloc(sizeof(struct mmc3416x_data), GFP_KERNEL);
	if (!s_mmc) {
		sDump_err(MAGNETIC.debugLevel, "%s: memory allocation failed.", __func__);
		res = -ENOMEM;
		goto out;
	}
	
if(mmc3416x_powerSetting(&client->dev, true) == false){
		sDump_err(MAGNETIC.debugLevel, "%s power setting failed", __func__);
		goto exit1;
	}
	if(mmc3416x_setPower(&client->dev, true) == false){
		sDump_err(MAGNETIC.debugLevel, "%s enabled power failed", __func__);
		goto exit2;
	}

#if 0	
	res = device_create_file(&client->dev, &dev_attr_mmc3416x);
	if (res) {
		pr_err("%s: device_create_file failed\n", __FUNCTION__);
		goto out_deregister;
	}

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_REFILL;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_SET);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_RESET;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = 0;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_SET);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_REFILL;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_RESET);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_SET;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);
	data[0] = MMC3416X_REG_CTRL;
	data[1] = 0;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(1);

	data[0] = MMC3416X_REG_BITS;
	data[1] = MMC3416X_BITS_SLOW_16;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_TM);

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_TM;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_TM);

#endif

	
	Sensor = kzalloc(sizeof(Magnetic), GFP_KERNEL);
	input_dev = input_allocate_device();

	if(!Sensor || !input_dev){
		res = -ENOMEM;
		goto err_free_mem;
	}
	
	INIT_DELAYED_WORK(&Sensor->dw, mmc3415x_work_func);
       input_dev->name = "magnetic";
       Magnetic_WorkQueue = create_singlethread_workqueue(input_dev->name);
     i2c_set_clientdata(client, Sensor);
	   
       sensor_device_register(&client->dev, &MAGNETIC);
       
	MagneticSleepTime=msecs_to_jiffies(200);   
	//mmc3416x_input_init(client);
	mutex_init(&lock);
sDump_debug(MAGNETIC.debugLevel, "%s-- \n", __func__);

	return 0;

//out_deregister:
	//misc_deregister(&mmc3416x_device);
exit1:
	kfree(s_mmc);	
exit2:
	mmc3416x_powerSetting(&client->dev, false);	
err_free_mem:
		if(input_dev != NULL)
			input_free_device(input_dev);
		kfree(Sensor);
out:
	return res;
}

static ssize_t mmc3416x_fs_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned char data[6] = {0};
	int vec[3] = {0};
	int count;
	int res = 0;

	mutex_lock(&ecompass_lock);

        data[0] = MMC3416X_REG_CTRL;
        data[1] = MMC3416X_CTRL_TM;
        res = mmc3xxx_i2c_tx_data(data, 2);

        msleep(MMC3416X_DELAY_TM);

        data[0] = MMC3416X_REG_DATA;
	if (mmc3xxx_i2c_rx_data(data, 6) < 0) {
	    return 0;
	}
	vec[0] = data[1] << 8 | data[0];
	vec[1] = data[3] << 8 | data[2];
	vec[2] = data[5] << 8 | data[4];
	vec[2] = 65536 - vec[2];	
	count = sprintf(buf,"%d,%d,%d\n", vec[0], vec[1], vec[2]);
	mutex_unlock(&ecompass_lock);

	return count;
}

static ssize_t mmc3416x_fs_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned char data[16] = {0};

	data[0] = MMC3416X_REG_CTRL;
	data[1] = MMC3416X_CTRL_TM;
	if (mmc3xxx_i2c_tx_data(data, 2) < 0) {
	}
	msleep(MMC3416X_DELAY_TM);

	return size;
}

static int mmc3416x_remove(struct i2c_client *client)
{
#if 0
	device_remove_file(&client->dev, &dev_attr_mmc3416x);
	misc_deregister(&mmc3416x_device);
	
	if (ipdev) input_unregister_polled_device(ipdev);
       #endif
	return 0;
}

static DEVICE_ATTR(read_mag, S_IRUGO | S_IWUSR | S_IWGRP, mmc3416x_fs_read, mmc3416x_fs_write);

static const struct i2c_device_id mmc3416x_id[] = {
	{ MMC3416X_I2C_NAME, 0 },
	{ }
};

static struct of_device_id match_table[] = {
	{ .compatible = "magnetic,mmc3416x",},
	{ },
};
static struct i2c_driver mmc3416x_driver = {
	.probe 		= mmc3416x_probe,
	.remove 	= mmc3416x_remove,
	.id_table	= mmc3416x_id,
	.driver 	= {
		.owner	= THIS_MODULE,
		.name	= MMC3416X_I2C_NAME,
		.of_match_table = match_table,
	},
};


static int __init mmc3416x_init(void)
{
	struct device *dev_t;

	mag_class = class_create(THIS_MODULE, "magnetic");

	if (IS_ERR(mag_class)) 
		return PTR_ERR( mag_class );

	dev_t = device_create( mag_class, NULL, 0, "%s", "magnetic");

	if (device_create_file(dev_t, &dev_attr_read_mag) < 0)
		printk("Failed to create device file(%s)!\n", dev_attr_read_mag.attr.name);

	if (IS_ERR(dev_t)) 
	{
		return PTR_ERR(dev_t);
	}
        printk("mmc3416x add driver\r\n");
	ipdev = NULL;
	return i2c_add_driver(&mmc3416x_driver);
}

static void __exit mmc3416x_exit(void)
{
        i2c_del_driver(&mmc3416x_driver);
}

module_init(mmc3416x_init);
module_exit(mmc3416x_exit);

MODULE_DESCRIPTION("MEMSIC MMC3416X Magnetic Sensor Driver");
MODULE_LICENSE("GPL");

