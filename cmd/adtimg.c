// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2018 Linaro Ltd.
 * Sam Protsenko <semen.protsenko@linaro.org>
 * Eugeniu Rosca <rosca.eugeniu@gmail.com>
 */

#include <env.h>
#include <image-android-dt.h>
#include <common.h>
#include <linux/ctype.h>   /* isalpha */

#define OPT_INDEX	"--index"
#define OPT_ID		"--id"
#define OPT_REV		"--rev"
#define OPT_MAXLEN	8

#ifdef pr_err
#undef pr_err
#endif

#define pr_err(fmt, args...) printf("Error: " fmt, ##args)

/*
 * Current/working DTB/DTBO Android image address.
 * Similar to 'working_fdt' variable in 'fdt' command.
 */
static ulong working_img;

static int do_adtimg_addr(cmd_tbl_t *cmdtp, int flag, int argc,
			  char * const argv[])
{
	char *endp;
	ulong hdr_addr;

	if (argc != 2)
		return CMD_RET_USAGE;

	hdr_addr = simple_strtoul(argv[1], &endp, 16);
	if (*endp != '\0') {
		pr_err("Wrong image address '%s'\n", argv[1]);
		return CMD_RET_FAILURE;
	}

	/*
	 * Allow users to set an address prior to copying the DTB/DTBO
	 * image to that same address, i.e. skip header verification.
	 */

	working_img = hdr_addr;
	return CMD_RET_SUCCESS;
}

static int adtimg_check_working_img(void)
{
	if (!working_img) {
		pr_err("Please, call 'adtimg addr <addr>'. Aborting!\n");
		return CMD_RET_FAILURE;
	}

	if (!android_dt_check_header(working_img)) {
		pr_err("Invalid image header at 0x%lx\n", working_img);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

static int do_adtimg_dump(cmd_tbl_t *cmdtp, int flag, int argc,
			  char * const argv[])
{
	if (argc != 1)
		return CMD_RET_USAGE;

	if (adtimg_check_working_img() != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	android_dt_print_contents(working_img);

	return CMD_RET_SUCCESS;
}

static int adtimg_getopt_u32(char * const opt, char * const name, u32 *optval)
{
	char *endp, *str;
	u32 val;

	if (!opt || !name || !optval)
		return CMD_RET_FAILURE;

	str = strchr(opt, '=');
	if (!str) {
		pr_err("Option '%s' not followed by '='\n", name);
		return CMD_RET_FAILURE;
	}

	if (*++str == '\0') {
		pr_err("Option '%s=' not followed by value\n", name);
		return CMD_RET_FAILURE;
	}

	val = simple_strtoul(str, &endp, 0);
	if (*endp != '\0') {
		pr_err("Wrong integer value '%s=%s'\n", name, str);
		return CMD_RET_FAILURE;
	}

	*optval = val;
	return CMD_RET_SUCCESS;
}

static int adtimg_getopt_index(int argc, char * const argv[], u32 *index,
			       char **avar, char **svar)
{
	int ret;

	if (!argv || !avar || !svar)
		return CMD_RET_FAILURE;

	if (argc > 3) {
		pr_err("Unexpected argument '%s'\n", argv[3]);
		return CMD_RET_FAILURE;
	}

	ret = adtimg_getopt_u32(argv[0], OPT_INDEX, index);
	if (ret != CMD_RET_SUCCESS)
		return ret;

	if (argc > 1)
		*avar = argv[1];
	if (argc > 2)
		*svar = argv[2];

	if (*avar && !isalpha(**avar)) {
		pr_err("Cannot use '%s' as environment variable\n", *avar);
		return CMD_RET_FAILURE;
	}
	if (*svar && !isalpha(**svar)) {
		pr_err("Cannot use '%s' as environment variable\n", *svar);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

static int adtimg_get_dt_by_index(int argc, char * const argv[])
{
	ulong addr;
	u32 index, size;
	int ret;
	char *avar = NULL, *svar = NULL;

	ret = adtimg_getopt_index(argc, argv, &index, &avar, &svar);
	if (ret != CMD_RET_SUCCESS)
		return ret;

	if (!android_dt_get_fdt_by_index(working_img, index, &addr, &size))
		return CMD_RET_FAILURE;

	if (avar && svar) {
		env_set_hex(avar, addr);
		env_set_hex(svar, size);
	} else if (avar) {
		env_set_hex(avar, addr);
		printf("0x%x (%d)\n", size, size);
	} else {
		printf("0x%lx, 0x%x (%d)\n", addr, size, size);
	}

	return CMD_RET_SUCCESS;
}

static int adtimg_getopt_fields(int argc, char * const argv[],
				struct dt_table_entry *dte,
				char **avar, char **svar, char **ivar)
{
	bool found = false;

	if (!dte || !argv || !avar || !svar || !ivar)
		return CMD_RET_FAILURE;

	for (int i = 0; i < argc; i++) {
		char *opt = argv[i];
		struct adtimg_opt_dt_map {
			char optname[OPT_MAXLEN];
			u32 *optptr;
		} t[] = {
			{ OPT_ID, &dte->id },
			{ OPT_REV, &dte->rev },
		};
		int j, ret, cnt = ARRAY_SIZE(t);

		for (j = 0; j < cnt; j++) {
			char *name = t[j].optname;
			u32 *ptr = t[j].optptr;

			if (!strncmp(argv[i], name, strlen(name))) {
				ret = adtimg_getopt_u32(opt, name, ptr);
				if (ret != CMD_RET_SUCCESS)
					return ret;

				if (!*ptr) {
					/*
					 * 'Zero' means 'unused', hence
					 * forbid zero values in user input
					 */
					pr_err("0 not allowed in '%s'\n", opt);
					return CMD_RET_FAILURE;
				}

				found = true;
				break;
			}
		}

		if (j < cnt)
			continue;

		if (!isalpha(*opt)) {
			pr_err("Option '%s' not supported\n", opt);
			return CMD_RET_FAILURE;
		}

		if (!*avar) {
			*avar = opt;
		} else if (!*svar) {
			*svar = opt;
		} else if (!*ivar) {
			*ivar = opt;
		} else {
			pr_err("Option '%s' not expected\n", opt);
			return CMD_RET_FAILURE;
		}
	}

	if (!found) {
		pr_err("No --option given (check usage)\n");
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

static int adtimg_get_dt_by_field(int argc, char * const argv[])
{
	char *avar = NULL, *svar = NULL, *ivar = NULL;
	struct dt_table_entry dte = {0};
	ulong addr;
	u32 sz, idx;
	int ret;

	ret = adtimg_getopt_fields(argc, argv, &dte, &avar, &svar, &ivar);
	if (ret != CMD_RET_SUCCESS)
		return ret;

	if (!android_dt_get_fdt_by_field(working_img, &dte, &addr, &sz, &idx))
		return CMD_RET_FAILURE;

	if (avar && svar && ivar) {
		env_set_hex(avar, addr);
		env_set_hex(svar, sz);
		env_set_hex(ivar, idx);
	} else if (avar && svar) {
		env_set_hex(avar, addr);
		env_set_hex(svar, sz);
		printf("0x%x (%d)\n", idx, idx);
	} else if (avar) {
		env_set_hex(avar, addr);
		printf("0x%x (%d), 0x%x (%d)\n", sz, sz, idx, idx);
	} else {
		printf("0x%lx, 0x%x (%d), 0x%x (%d)\n", addr, sz, sz, idx, idx);
	}

	return CMD_RET_SUCCESS;
}

static int adtimg_get_dt(int argc, char * const argv[])
{
	if (argc < 2) {
		pr_err("No options passed to '%s'\n", argv[0]);
		return CMD_RET_FAILURE;
	}

	/* Strip off leading 'dt' command argument */
	argc--;
	argv++;

	if (!strncmp(argv[0], OPT_INDEX, sizeof(OPT_INDEX) - 1))
		return adtimg_get_dt_by_index(argc, argv);

	return adtimg_get_dt_by_field(argc, argv);
}

static int do_adtimg_get(cmd_tbl_t *cmdtp, int flag, int argc,
			 char * const argv[])
{
	if (argc < 2) {
		pr_err("No arguments passed to '%s'\n", argv[0]);
		return CMD_RET_FAILURE;
	}

	if (adtimg_check_working_img() != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	/* Strip off leading 'get' command argument */
	argc--;
	argv++;

	if (!strcmp(argv[0], "dt"))
		return adtimg_get_dt(argc, argv);

	pr_err("Wrong argument '%s'\n", argv[0]);
	return CMD_RET_FAILURE;
}

static cmd_tbl_t cmd_adtimg_sub[] = {
	U_BOOT_CMD_MKENT(addr, CONFIG_SYS_MAXARGS, 1, do_adtimg_addr, "", ""),
	U_BOOT_CMD_MKENT(dump, CONFIG_SYS_MAXARGS, 1, do_adtimg_dump, "", ""),
	U_BOOT_CMD_MKENT(get, CONFIG_SYS_MAXARGS, 1, do_adtimg_get, "", ""),
};

static int do_adtimg(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	cmd_tbl_t *cp;

	cp = find_cmd_tbl(argv[1], cmd_adtimg_sub, ARRAY_SIZE(cmd_adtimg_sub));

	/* Strip off leading 'adtimg' command argument */
	argc--;
	argv++;

	if (!cp || argc > cp->maxargs)
		return CMD_RET_USAGE;
	if (flag == CMD_FLAG_REPEAT && !cmd_is_repeatable(cp))
		return CMD_RET_SUCCESS;

	return cp->cmd(cmdtp, flag, argc, argv);
}

U_BOOT_CMD(
	adtimg, CONFIG_SYS_MAXARGS, 0, do_adtimg,
	"manipulate dtb/dtbo Android image",
	"addr <addr> - Set image location to <addr>\n"
	"adtimg dump        - Print out image contents\n"
	"adtimg get dt --index=<index> [avar [svar]]         - Get DT address/size by index\n"
	"adtimg get dt --<fname>=<fval> [avar [svar [ivar]]] - Get DT address/size/index\n"
	"                                                      by field in dt_table_entry\n"
	"Legend:\n"
	"  - <addr>: DTB/DTBO image address (hex) in RAM\n"
	"  - <index>: index (hex/dec) of desired DT in the image\n"
	"  - <fname>: dt_table_entry field name. Supported values: id, rev\n"
	"  - <fval>: field value (hex/dec) associated to <fname>\n"
	"  - <avar>: variable name to contain DT address (hex)\n"
	"  - <svar>: variable name to contain DT size (hex)\n"
	"  - <ivar>: variable name to contain DT index (hex)"
);
