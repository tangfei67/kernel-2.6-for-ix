#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <asm/numa.h>
#include <asm/pci_x86.h>

struct pci_root_info {
	char *name;
	unsigned int res_num;
	struct resource *res;
	struct pci_bus *bus;
	int busnum;
};

static bool pci_use_crs;

static int __init set_use_crs(const struct dmi_system_id *id)
{
	pci_use_crs = true;
	return 0;
}

static const struct dmi_system_id pci_use_crs_table[] __initconst = {
	/* http://bugzilla.kernel.org/show_bug.cgi?id=14183 */
	{
		.callback = set_use_crs,
		.ident = "IBM System x3800",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3800"),
		},
	},
	{
		.callback = set_use_crs,
		.ident = "HP ProLiant DL980 G7",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL980 G7"),
		},
	},
	{
		.callback = set_use_crs,
		.ident = "HP ProLiant DL370 G6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL370 G6"),
		},
	},
	{
		.callback = set_use_crs,
		.ident = "HP ProLiant ML370 G6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant ML370 G6"),
		},
	},
	{}
};

void __init pci_acpi_crs_quirks(void)
{
	/* We cannot take this whitelist by date for SLES 11 SP1 or
	 * customers might see breakage because of a simple BIOS update
	 *
	 * int year;
	 *
	 * if (dmi_get_date(DMI_BIOS_DATE, &year, NULL, NULL) && year < 2008)
	 *      pci_use_crs = false;
	*/
	dmi_check_system(pci_use_crs_table);

	/*
	 * If the user specifies "pci=use_crs" or "pci=nocrs" explicitly, that
	 * takes precedence over anything we figured out above.
	 */
	if (pci_probe & PCI_ROOT_NO_CRS)
		pci_use_crs = false;
	else if (pci_probe & PCI_USE__CRS)
		pci_use_crs = true;

	printk(KERN_INFO "PCI: %s host bridge windows from ACPI; "
	       "if necessary, use \"pci=%s\" and report a bug\n",
	       pci_use_crs ? "Using" : "Ignoring",
	       pci_use_crs ? "nocrs" : "use_crs");
}

static acpi_status
resource_to_addr(struct acpi_resource *resource,
			struct acpi_resource_address64 *addr)
{
	acpi_status status;

	status = acpi_resource_to_address64(resource, addr);
	if (ACPI_SUCCESS(status) &&
	    (addr->resource_type == ACPI_MEMORY_RANGE ||
	    addr->resource_type == ACPI_IO_RANGE) &&
	    addr->address_length > 0 &&
	    addr->producer_consumer == ACPI_PRODUCER) {
		return AE_OK;
	}
	return AE_ERROR;
}

static acpi_status
count_resource(struct acpi_resource *acpi_res, void *data)
{
	struct pci_root_info *info = data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = resource_to_addr(acpi_res, &addr);
	if (ACPI_SUCCESS(status))
		info->res_num++;
	return AE_OK;
}

static acpi_status
setup_resource(struct acpi_resource *acpi_res, void *data)
{
	struct pci_root_info *info = data;
	struct resource *res;
	struct acpi_resource_address64 addr;
	acpi_status status;
	unsigned long flags;
	struct resource *root;
	u64 start, end, max_len;

	status = resource_to_addr(acpi_res, &addr);
	if (!ACPI_SUCCESS(status))
		return AE_OK;

	if (addr.resource_type == ACPI_MEMORY_RANGE) {
		root = &iomem_resource;
		flags = IORESOURCE_MEM;
		if (addr.info.mem.caching == ACPI_PREFETCHABLE_MEMORY)
			flags |= IORESOURCE_PREFETCH;
	} else if (addr.resource_type == ACPI_IO_RANGE) {
		root = &ioport_resource;
		flags = IORESOURCE_IO;
	} else
		return AE_OK;

	max_len = addr.maximum - addr.minimum + 1;
	if (addr.address_length > max_len) {
		printk(KERN_DEBUG
			   "host bridge window length %#llx doesn't fit in "
			   "%#llx-%#llx, trimming\n",
			   (unsigned long long) addr.address_length,
			   (unsigned long long) addr.minimum,
			   (unsigned long long) addr.maximum);
		addr.address_length = max_len;
	}

	start = addr.minimum + addr.translation_offset;
	end = start + addr.address_length - 1;

	res = &info->res[info->res_num];
	res->name = info->name;
	res->flags = flags;
	res->start = start;
	res->end = end;
	res->child = NULL;

	if (insert_resource(root, res)) {
		printk(KERN_ERR "PCI: Failed to allocate 0x%lx-0x%lx "
			"from %s for %s\n", (unsigned long) res->start,
			(unsigned long) res->end, root->name, info->name);
	} else {
		pci_bus_add_resource(info->bus, res, 0);
		info->res_num++;
	}
	return AE_OK;
}

static void
get_current_resources(struct acpi_device *device, int busnum,
			int domain, struct pci_bus *bus)
{
	struct pci_root_info info;
	size_t size;

	if (pci_use_crs)
 		pci_bus_remove_resources(bus);

	info.bus = bus;
	info.res_num = 0;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, count_resource,
				&info);
	if (!info.res_num)
		return;

	size = sizeof(*info.res) * info.res_num;
	info.res = kmalloc(size, GFP_KERNEL);
	if (!info.res)
		goto res_alloc_fail;

	info.name = kmalloc(16, GFP_KERNEL);
	if (!info.name)
		goto name_alloc_fail;
	sprintf(info.name, "PCI Bus %04x:%02x", domain, busnum);

	info.res_num = 0;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, setup_resource,
				&info);

	return;

name_alloc_fail:
	kfree(info.res);
res_alloc_fail:
	return;
}

struct pci_bus * __devinit pci_acpi_scan_root(struct acpi_device *device, int domain, int busnum)
{
	struct pci_bus *bus;
	struct pci_sysdata *sd;
	int node;
#ifdef CONFIG_ACPI_NUMA
	int pxm;
#endif

	if (domain && !pci_domains_supported) {
		printk(KERN_WARNING "PCI: Multiple domains not supported "
		       "(dom %d, bus %d)\n", domain, busnum);
		return NULL;
	}

	node = -1;
#ifdef CONFIG_ACPI_NUMA
	pxm = acpi_get_pxm(device->handle);
	if (pxm >= 0)
		node = pxm_to_node(pxm);
	if (node != -1)
		set_mp_bus_to_node(busnum, node);
	else
#endif
		node = get_mp_bus_to_node(busnum);

	if (node != -1 && !node_online(node))
		node = -1;

	/* Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_ERR "PCI: OOM, not probing PCI bus %02x\n", busnum);
		return NULL;
	}

	sd->domain = domain;
	sd->node = node;
	/*
	 * Maybe the desired pci bus has been already scanned. In such case
	 * it is unnecessary to scan the pci bus with the given domain,busnum.
	 */
	bus = pci_find_bus(domain, busnum);
	if (bus) {
		/*
		 * If the desired bus exits, the content of bus->sysdata will
		 * be replaced by sd.
		 */
		memcpy(bus->sysdata, sd, sizeof(*sd));
		kfree(sd);
	} else {
		bus = pci_create_bus(NULL, busnum, &pci_root_ops, sd);
		if (bus) {
			if (pci_use_crs)
				get_current_resources(device, busnum, domain,
						      bus);
			bus->subordinate = pci_scan_child_bus(bus);
		}
	}

	if (!bus)
		kfree(sd);

	if (bus && node != -1) {
#ifdef CONFIG_ACPI_NUMA
		if (pxm >= 0)
			dev_printk(KERN_DEBUG, &bus->dev,
				   "on NUMA node %d (pxm %d)\n", node, pxm);
#else
		dev_printk(KERN_DEBUG, &bus->dev, "on NUMA node %d\n", node);
#endif
	}

	return bus;
}

int __init pci_acpi_init(void)
{
	struct pci_dev *dev = NULL;

	if (pcibios_scanned)
		return 0;

	if (acpi_noirq)
		return 0;

	printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
	acpi_irq_penalty_init();
	pcibios_scanned++;
	pcibios_enable_irq = acpi_pci_irq_enable;
	pcibios_disable_irq = acpi_pci_irq_disable;

	if (pci_routeirq) {
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing PCI interrupts for all devices because \"pci=routeirq\" specified\n");
		for_each_pci_dev(dev)
			acpi_pci_irq_enable(dev);
	}

	return 0;
}
