#include <linux/module.h>
#include <linux/usb.h>
#include <linux/init.h>
#include <linux/kernel.h> // For printk
#include <linux/slab.h> // For kmalloc and kfree
#include <linux/device.h>


#define USB_TEMP_VENDOR_ID 0x16c0
#define USB_TEMP_PRODUCT_ID 0x05dc


/*
 *
 * Structs
 *
 */

struct temp{
    uint8_t full;
    uint8_t decimal;
};
// For attr storage
struct usb_interface_data{
    uint8_t probe_count;
    struct device_attribute** device_attributes;
};

// for long message
struct probe_status {
    uint8_t serial[6];
    uint8_t type;
    uint8_t flags;
    uint8_t temperature[2];
    uint32_t timestamp;
    uint8_t padding[2];
}__packed;

// for short message
struct short_status {
    uint8_t version_high;
    uint8_t version_low;
    uint32_t timestamp;
    uint8_t supported_probes;
    uint8_t padding;
}__packed;

// for Rescan
struct rescan_reply {
    uint8_t answer;
};

/*
 *
 * Function declarations
 *
 */
static void print_temp(uint8_t low, uint8_t high,struct temp* storage);
static void setup_sysfs(struct usb_device* usb_dev, struct usb_interface* interface);
static void deactivate_sysfs(struct usb_interface* interface);

static uint8_t usb_message_short(struct usb_device* dev);
static uint8_t usb_message_long(struct usb_device* dev, uint8_t possible_probes, int type,struct temp* storage);
static void usb_message_reset(struct usb_device* dev);
static int usb_message_rescan_status(struct usb_device* dev);
static void usb_message_rescan(struct usb_device* dev);

static ssize_t show(struct device *dev, struct device_attribute *attr,char *buf);
static ssize_t store(struct device *dev, struct device_attribute *attr,const char* buf,size_t count);

static ssize_t show_rescan(struct device *dev, struct device_attribute *attr,char *buf);
static ssize_t store_rescan(struct device *dev, struct device_attribute *attr,const char* buf,size_t count);

static ssize_t show_restart(struct device *dev, struct device_attribute *attr,char *buf);
static ssize_t store_restart(struct device *dev, struct device_attribute *attr,const char* buf,size_t count);

static void delete_old_probes(struct usb_interface* interface);
static void add_new_probes(struct usb_device* usb_dev, struct usb_interface* interface);
/*
 *
 * Helper Functions
 *
 */

static void print_temp(uint8_t low, uint8_t high, struct temp* storage){
        int temperature = low + (high << 8);
        if(0x800 & temperature){
           temperature = temperature || 0xFFFFF000;
        }
        int val = temperature / 4;
        temperature = temperature / 16;
        val = val * (temperature*4);
        storage -> full = temperature;
        storage -> decimal = val;
}

static void setup_sysfs(struct usb_device* usb_dev, struct usb_interface* interface){
    // Get all possiple probes
    uint8_t possible_probes = usb_message_short(usb_dev); 
    // Wait for rescan
    // usb_message_rescan(usb_dev);
    while(usb_message_rescan_status(usb_dev) != 1){
    }
    // Get the amount of real probes
    uint8_t real_probes = usb_message_long(usb_dev, possible_probes,-1, NULL); 
    // Generate file for every real probe
    pr_info("make files: with %d probes\n",real_probes);
    // Setup data for usb interface
    struct usb_interface_data* data = kmalloc(sizeof(struct usb_interface_data), GFP_KERNEL);
    if(data == NULL){
        pr_err("Error kmalloc");
        return;
    }
    data -> probe_count = real_probes;
    data -> device_attributes = kmalloc(sizeof(struct device_attribute*)*(real_probes+2), GFP_KERNEL);
    // Generate file for the general USB device
    for(int i = 2; i <= real_probes + 1; i++){
        // both need to be freed
        struct device_attribute* atr = kmalloc(sizeof(struct device_attribute), GFP_KERNEL) ;
        if(atr == NULL){
            pr_err("Error kmalloc");
            continue;
        }
        char* number =  kmalloc(sizeof(char) * 16, GFP_KERNEL);
        if(number == NULL){
            kfree(atr);
            pr_err("Error kmalloc");
            continue;
        }
        snprintf(number, 16, "%d",i-2);
        int size = 6 + strlen(number);
        char* name = kmalloc(size, GFP_KERNEL); //probe X\0 
        if(name == NULL){
            kfree(atr);
            kfree(number);
            pr_err("Error kmalloc");
            continue;
        }
        name[0] = 'p'; 
        name[1] = 'r'; 
        name[2] = 'o'; 
        name[3] = 'b'; 
        name[4] = 'e'; 
        // only works for values 0-9 need to implement function to generate string from integer
        for(int i = 0; i < strlen(number); i++)
        {
            name[5 + i] = number[i];
        }
        name[size-1] = '\0'; 
        kfree(number);
        atr -> attr.name = name;
        atr -> attr.mode = S_IWUSR | S_IRUGO;
        atr -> show = &show; 
        atr -> store = &store; 
        int error = device_create_file(&(interface->dev), atr);
        if(error){
            pr_err("failed %d\n",error);
            kfree(name);
            kfree(atr);
            data -> device_attributes[i] = NULL;
        }else{
            pr_info("Filename:  %s\n",name);
            data -> device_attributes[i] = atr;
        }
    }
    // General Device Setup
    struct device_attribute* atr = kmalloc(sizeof(struct device_attribute), GFP_KERNEL);
    if(atr == NULL)
    {
        pr_err("Error kmalloc");
    }else{
        atr -> attr.name = "temp_rescan";
        atr -> attr.mode = S_IWUSR | S_IRUGO;
        atr -> show = &show_rescan;
        atr -> store = &store_rescan;
        int error = device_create_file(&(interface->dev), atr);
        if(error){
            pr_err("failed %d\n",error);
            kfree(atr);
            data -> device_attributes[0] = NULL;
        }else{
            data -> device_attributes[0] = atr;
        }
    }


    atr = kmalloc(sizeof(struct device_attribute), GFP_KERNEL);
    if(atr == NULL)
    {
        pr_err("Error kmalloc");
    }else{
        atr -> attr.name = "temp_restart";
        atr -> attr.mode = S_IWUSR | S_IRUGO;
        atr -> show = &show_restart;
        atr -> store = &store_restart;
        int error = device_create_file(&(interface->dev), atr);
        if(error){
            pr_err("failed %d\n",error);
            kfree(atr);
            data -> device_attributes[1] = NULL;
        }else{
            data -> device_attributes[1] = atr;
        }
    }
    usb_set_intfdata(interface,data);
}

static void deactivate_sysfs(struct usb_interface* interface) {
    struct usb_interface_data* data = usb_get_intfdata(interface); 
    for(int i = 2; i <= (data -> probe_count) + 1; i++){
        if(data -> device_attributes[i] != NULL)
        {
            device_remove_file(&(interface->dev), data -> device_attributes[i]);
            kfree(data -> device_attributes[i] -> attr.name);
            kfree(data -> device_attributes[i]);
        } 
    }
    // Clean up Rescan & restart
    if(data -> device_attributes[0] != NULL)
    {
        device_remove_file(&(interface->dev), data -> device_attributes[0]);
        kfree(data -> device_attributes[0]);
    }
    if(data -> device_attributes[1] != NULL)
    {
        device_remove_file(&(interface->dev), data -> device_attributes[0]);
        kfree(data -> device_attributes[1]);
    }
    kfree(data -> device_attributes);
    kfree(data);
}

static void delete_old_probes(struct usb_interface* interface){
    struct usb_interface_data* data = usb_get_intfdata(interface); 
    for(int i = 2; i <= (data -> probe_count) + 1; i++){
        if(data -> device_attributes[i] != NULL)
        {
            device_remove_file(&(interface->dev), data -> device_attributes[i]);
            kfree(data -> device_attributes[i] -> attr.name);
            kfree(data -> device_attributes[i]);
        } 
    }
}

static void add_new_probes(struct usb_device* usb_dev, struct usb_interface* interface)
{
    // Wait for rescan
    while(usb_message_rescan_status(usb_dev) != 1)
    {
        // wait for 0.25s to not overwhelm the microprocessor
        msleep(250);
    }
    // Get all possiple probes
    pr_info("call short\n");
    uint8_t possible_probes = usb_message_short(usb_dev); 
    // Get the amount of real probes
    pr_info("call long\n");
    uint8_t real_probes = usb_message_long(usb_dev, possible_probes,-1, NULL); 
    // Generate file for every real probe
    pr_info("make files: wiht %d probes\n",real_probes);
    // Setup data for usb interface
    struct usb_interface_data* data = kmalloc(sizeof(struct usb_interface_data), GFP_KERNEL);
    if(data == NULL)
    {
        pr_err("Error kmalloc");
        return;
    }
    data -> probe_count = real_probes;
    data -> device_attributes = kmalloc(sizeof(struct device_attribute*)*(real_probes+2), GFP_KERNEL);
    if(data -> device_attributes == NULL)
    {
        pr_err("Error kmalloc");
        return;
    }
    // Generate file for the general USB device
    for(int i = 2; i <= real_probes + 1; i++){
        // both need to be freed
        struct device_attribute* atr = kmalloc(sizeof(struct device_attribute), GFP_KERNEL) ;
        if(atr == NULL){
            pr_err("Error kmalloc");
            continue;
        }
        char* number =  kmalloc(sizeof(char) * 16, GFP_KERNEL);
        if(number == NULL){
            kfree(atr);
            pr_err("Error kmalloc");
            continue;
        }
        snprintf(number, 16, "%d",i-2);
        int size = 6 + strlen(number);
        char* name = kmalloc(size, GFP_KERNEL); //probe X\0 
        if(name == NULL){
            kfree(atr);
            kfree(number);
            pr_err("Error kmalloc");
            continue;
        }
        name[0] = 'p'; 
        name[1] = 'r'; 
        name[2] = 'o'; 
        name[3] = 'b'; 
        name[4] = 'e'; 
        // only works for values 0-9 need to implement function to generate string from integer
        for(int i = 0; i < strlen(number); i++)
        {
            name[5 + i] = number[i];
        }
        name[size-1] = '\0'; 
        kfree(number);
        atr -> attr.name = name;
        atr -> attr.mode = S_IWUSR | S_IRUGO;
        atr -> show = &show; 
        atr -> store = &store; 
        int error = device_create_file(&(interface->dev), atr);
        if(error){
            pr_err("failed %d\n",error);
            kfree(name);
            kfree(atr);
            data -> device_attributes[i] = NULL;
        }else{
            pr_info("File created: %s\n",name);
            data -> device_attributes[i] = atr;
        }
    }
    // General Device Setup
    struct usb_interface_data* old_data = usb_get_intfdata(interface);
    data -> device_attributes[0] = old_data -> device_attributes[0];
    data -> device_attributes[1] = old_data -> device_attributes[1];
    kfree(old_data);
    usb_set_intfdata(interface,data);
}

/*
 *
 * Other Stuff
 *
 */
static uint8_t usb_message_long(struct usb_device* dev, uint8_t possible_probes, int type, struct temp* storage)
{
    __u8 request = 3;
    __u16 value = 0;
    __u16 index = 0;
    struct probe_status* data = kmalloc(sizeof(struct probe_status) * possible_probes, GFP_KERNEL);
    if(data == NULL){
        pr_info("Error kmalloc");
        return -1;
    }
    __u16 size = sizeof(struct probe_status)  * possible_probes;
    int timeout = 1000;
    int error = usb_control_msg(dev,usb_rcvctrlpipe(dev,0), request, 0xc0, value,index, data, size, timeout);
    uint8_t count = -1;
    if(error < 0)
    {
        pr_err("error %d \n",error);
    }
    else
    {
        // Finding all probes
        if (type == -1)
        {
            count = 0;
            // finding the amount of existing probes
            for(int i = 0; i < possible_probes; i++){
                pr_info("flags %d \n", data[i].flags);
                if(data[i].flags == 0x01){
                    count++;
                }
            } 
        }
        // Read from one probe
        else{
            int found = 0;
            for(int i = 0; i < possible_probes; i++){
                if(data[i].flags == 0x01){
                    if(found == type)
                    {
                        print_temp(data[type].temperature[0], data[type].temperature[1], storage);
                    }else{
                        found++;
                    }
                }
            } 
            print_temp(data[type].temperature[0], data[type].temperature[1], storage);
        }
    }
    kfree(data);
    return count;
}

static uint8_t usb_message_short(struct usb_device* dev)
{
    __u8 request = 1;
    __u16 value = 0;
    __u16 index = 0;
    struct short_status* data = kmalloc(sizeof(struct short_status), GFP_KERNEL);
    if(data == NULL){
        pr_err("Error kmalloc");
        return -1;
    }
    __u16 size = sizeof(*data);
    int timeout = 1000;
    int error = usb_control_msg(dev,usb_rcvctrlpipe(dev,0), request, 0xc0, value,index, data, size, timeout); 

    uint8_t probe_count = -1;

    if (error < 0)
    {
        pr_info("error  %d \n", error);
    }
    else
    {
       probe_count = data -> supported_probes;
    }
    kfree(data);
    return probe_count;
}

static void usb_message_reset(struct usb_device* dev)
{
    __u8 request = 4;
    __u16 value = 0;
    __u16 index = 0;
    struct short_status* data = kmalloc(8, GFP_KERNEL);
    if(data == NULL){
        pr_err("Error kmalloc");
        return;
    }

    __u16 size = sizeof(*data);
    int timeout = 1000;
    int error = usb_control_msg(dev,usb_rcvctrlpipe(dev,0), request, 0xc0, value,index, data, size, timeout); 
    if (error < 0)
    {
        pr_err("error %d\n",error);
    }

    kfree(data);

}


static void usb_message_rescan(struct usb_device* dev)
{
    __u8 request = 2;
    __u16 value = 0;
    __u16 index = 0;
    struct rescan_reply* data = kmalloc(sizeof(struct rescan_reply), GFP_KERNEL);
    if(data == NULL){
        pr_err("Error kmalloc");
        return;
    }

    __u16 size = sizeof(*data);
    int timeout = 1000;
    int error = usb_control_msg(dev,usb_rcvctrlpipe(dev,0), request, 0xc0, value,index, data, size, timeout); 
    if (error < 0)
    {
        pr_err("error  %d \n", error);
    }
    else
    {
       pr_info("Status %d\n",data -> answer); 
    }

    kfree(data);
}


static int usb_message_rescan_status(struct usb_device* dev)
{
    __u8 request = 2;
    __u16 value = 0x01;
    __u16 index = 0;
    struct rescan_reply* data = kmalloc(sizeof(struct rescan_reply), GFP_KERNEL);
    if(data == NULL){
        pr_err("Error kmalloc");
        return -1;
    }

    __u16 size = sizeof(*data);
    int timeout = 10;
    int error = usb_control_msg(dev,usb_rcvctrlpipe(dev,0), request, 0xc0, value,index, data, size, timeout); 
    int ret = 0;
    if (error < 0)
    {
        pr_err("error rescan %d \n", error);
    }
    else
    {
       if(data -> answer == 23){
            ret = 1;
       }
    }
    kfree(data);
    return ret;
}
static ssize_t show(struct device *dev, struct device_attribute *attr,char *buf)
{ 
    struct usb_interface* usb_inter;
    usb_inter = to_usb_interface(dev); //struct usb_device* usb_dev = usb_get_dev(usb_inter);
    struct usb_device* usb_dev = interface_to_usbdev(usb_inter);
    // Read probe pos from name
    long probe_pos;
    int error = kstrtol( &(attr -> attr.name[5]), 10, &probe_pos);
    if(error)
    {
        pr_err("kstrol error \n");
        return 0;
    }
    int probe_count = usb_message_short(usb_dev); 
    struct temp t = {};
    usb_message_long(usb_dev, probe_count, probe_pos, &t);
    return sysfs_emit(buf,"%d.%d\n",t.full,t.decimal);
}

static ssize_t store(struct device *dev, struct device_attribute *attr,const char* buf,size_t count)
{
    return count;
}

static ssize_t show_rescan(struct device *dev, struct device_attribute *attr,char *buf)
{
    struct usb_interface* usb_inter;
    usb_inter = to_usb_interface(dev); 
    struct usb_device* usb_dev = interface_to_usbdev(usb_inter);
    if(usb_message_rescan_status(usb_dev) == 1)
    {
        return sysfs_emit(buf,"scan done\n");
    }
    else
    {
        return sysfs_emit(buf,"scan not done\n");
    }

}

static ssize_t store_rescan(struct device *dev, struct device_attribute *attr,const char* buf,size_t count)
{
    struct usb_interface* usb_inter;
    usb_inter = to_usb_interface(dev); 
    struct usb_device* usb_dev = interface_to_usbdev(usb_inter);

    delete_old_probes(usb_inter);

    usb_message_rescan(usb_dev);

    add_new_probes(usb_dev, usb_inter);
    return count;
}

static ssize_t show_restart(struct device *dev, struct device_attribute *attr,char *buf)
{
    return 0;
}

static ssize_t store_restart(struct device *dev, struct device_attribute *attr,const char* buf,size_t count)
{
    struct usb_interface* usb_inter;
    usb_inter = to_usb_interface(dev); 
    struct usb_device* usb_dev = interface_to_usbdev(usb_inter);
    usb_message_reset(usb_dev);
    return count;
}

static int temp_probe(struct usb_interface* interface, const struct usb_device_id* id)
{
    pr_info("device connected\n");
    struct usb_device* usb_dev = interface_to_usbdev(interface);
    setup_sysfs(usb_dev, interface);
    return 0;
}
static void temp_disconnect(struct usb_interface* interface)
{
    deactivate_sysfs(interface);
}
static int temp_suspend(struct usb_interface *, struct pm_message)
{
    return 0;
}
static int temp_resume(struct usb_interface *)
{
    return 0;
}
static int temp_pre_reset(struct usb_interface *){
    return 0;
}
static int temp_post_reset(struct usb_interface *)
{
    return 0;
}
const struct usb_device_id temp_table[] = {
    { USB_DEVICE(USB_TEMP_VENDOR_ID, USB_TEMP_PRODUCT_ID)},
    {},
};
MODULE_DEVICE_TABLE(usb,temp_table);

static struct usb_driver temp_driver = {
        .name        = "tempsensor",
        .probe       = temp_probe,
        .disconnect  = temp_disconnect,
        .suspend     = temp_suspend,
        .resume      = temp_resume,
        .pre_reset   = temp_pre_reset,
        .post_reset  = temp_post_reset,
        .id_table    = temp_table,
        .supports_autosuspend = 1,
};


//static struct module owner;

static int __init simple_module_init(void)
{
    int result;
    result = usb_register(&temp_driver);
    if(result < 0)
    {
        pr_err("usb_regier failed! %s driver; Error code %d \n",temp_driver.name,result);
        return -1;
    }
	pr_info("module loaded\n");
	return 0;
}

static  void __exit simple_module_exit(void)
{
    usb_deregister(&temp_driver);
	pr_info("module unloaded\n");
}
module_init(simple_module_init);
module_exit(simple_module_exit);

MODULE_LICENSE("GPL");
