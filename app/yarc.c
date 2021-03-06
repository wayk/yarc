
/**
 * YARC: Yet Another Resource Compiler
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <yarc/yarc.h>

#ifndef _WIN32
#define _strdup strdup
#else
#pragma warning(disable: 4996)
#endif

#ifdef YARC_LZ4
#include <lz4/lz4.h>
#include <lz4/lz4hc.h>
#endif

typedef struct
{
	FILE* fp;
	size_t size;
	size_t offset;
	uint8_t* data;
	char* filename;
	char* basename;
	char* identifier;
} yarc_file_t;

static int yarc_width = 16;
static int yarc_padding = 2;
static char* yarc_list = NULL;
static bool yarc_upper = false;
static char* yarc_prefix = "yarc";
static char* yarc_bundle = "default";
static char* yarc_output = "resources.c";
static char* yarc_version = "1.0.0";
static bool yarc_static = false;
static bool yarc_verbose = false;
static bool yarc_compress = false;
static bool yarc_block = false;
static bool yarc_extract = false;

#ifndef YARC_LZ4
int LZ4_compress_HC(const char* src, char* dst, int srcSize, int dstCapacity, int compressionLevel) { return 0; }
#endif

const char* yarc_get_base_name(const char* filename)
{
	size_t length;
	char* separator;

	if (!filename)
		return NULL;

	separator = strrchr(filename, '\\');

	if (!separator)
		separator = strrchr(filename, '/');

	if (!separator)
		return filename;

	length = strlen(filename);

	if ((length - (separator - filename)) > 1)
		return separator + 1;

	return filename;
}

int yarc_file_construct_names(yarc_file_t* yf)
{
	char* p;
	size_t length;
	size_t size;
	const char* basename = yarc_get_base_name(yf->filename);

	length = strlen(basename);
	yf->basename = _strdup(basename);

	if (!yf->basename)
		return -1;

	size = length + strlen(yarc_bundle) + strlen(yarc_prefix) + 3;
	yf->identifier = malloc(size);

	if (!yf->identifier)
		return -1;

	snprintf(yf->identifier, size, "%s_%s_%s", yarc_prefix, yarc_bundle, yf->basename);

	p = yf->identifier;

	while (*p)
	{
		switch (*p)
		{
			case '.':
			case ',':
			case '-':
			case ' ':
			case '$':
			case '&':
			case '*':
			case '#':
			case '/':
			case '@':
			case '(':
			case ')':
			case '[':
			case ']':
			case '<':
			case '>':
			case '?':
			case '~':
			case '!':
			case '%':
			case '^':
			case '=':
			case '\\':
				*p = '_';
				break;
		}

		p++;
	}

	return 1;
}

int yarc_file_write(yarc_file_t* out, yarc_file_t* in)
{
	int i;
	int width = yarc_width;
	const uint8_t* data = in->data;
	const char* identifier = in->identifier;
	size_t size = in->size + yarc_padding;

	fprintf(out->fp, "/* %s */\n", in->basename);
	fprintf(out->fp, "%sconst unsigned int %s_size = %d;\n",
		yarc_static ? "static " : "", identifier, (int) in->size);

	if (yarc_compress)
	{
		fprintf(out->fp, "%sconst unsigned char* %s_data = 0;\n\n",
			yarc_static ? "static " : "", identifier);
		return 1;
	}

	fprintf(out->fp, "%sconst unsigned char %s[%d+%d] = {\n",
		yarc_static ? "static " : "", identifier, (int) in->size, yarc_padding);

	for (i = 0; i < (int) size; i++)
	{
		if ((i % width) == 0)
			fprintf(out->fp, "  ");

		fprintf(out->fp, yarc_upper ? "0x%02X" : "0x%02x", (int) data[i]);

		if (i != (size - 1))
			fprintf(out->fp, ", ");

		if (((i % width) == (width - 1)) || (i == (size - 1)))
			fprintf(out->fp, "\n");
	}

	fprintf(out->fp, "};\n");
	fprintf(out->fp, "%sconst unsigned char* %s_data = (unsigned char*) %s;\n",
		yarc_static ? "static " : "", identifier, identifier);
	fprintf(out->fp, "\n");

	return 1;
}

int yarc_zdata_write(yarc_file_t* out, const uint8_t* zdata, size_t zsize, size_t size)
{
	int i;
	int width = yarc_width;

	fprintf(out->fp, "%sconst unsigned char %s_%s_zdata[%d] = {\n",
		yarc_static ? "static " : "", yarc_prefix, yarc_bundle, (int) zsize);

	for (i = 0; i < (int) zsize; i++)
	{
		if ((i % width) == 0)
			fprintf(out->fp, "  ");

		fprintf(out->fp, yarc_upper ? "0x%02X" : "0x%02x", (int) zdata[i]);

		if (i != (zsize - 1))
			fprintf(out->fp, ", ");

		if (((i % width) == (width - 1)) || (i == (zsize - 1)))
			fprintf(out->fp, "\n");
	}

	fprintf(out->fp, "};\n");
	fprintf(out->fp, "\n");

	return 1;
}

size_t yarc_string_size(const char* str)
{
	size_t len, pad, size;
	len = str ? strlen(str) : 0;
	size = (2 + len + 1);
	pad = ((size + 3) & ~0x3) - size;
	return (size + pad);
}

size_t yarc_string_write(uint8_t* ptr, const char* str)
{
	size_t len, pad, size;
	len = str ? strlen(str) : 0;
	size = (2 + len + 1);
	pad = ((size + 3) & ~0x3) - size;
	*((uint16_t*) &ptr[0]) = (uint16_t) len;
	memcpy(&ptr[2], str, len + 1);
	memset(&ptr[size], 0, pad);
	return (size + pad);
}

int yarc_file_load(yarc_file_t* yf)
{
	yf->fp = fopen(yf->filename, "rb");

	if (!yf->fp)
		return -1;

	fseek(yf->fp, 0, SEEK_END);
	yf->size = (int) ftell(yf->fp);
	fseek(yf->fp, 0, SEEK_SET);

	yf->data = (uint8_t*) malloc(yf->size + yarc_padding);

	if (!yf->data)
		return -1;

	if (fread(yf->data, 1, yf->size, yf->fp) != yf->size)
		return -1;

	memset(&yf->data[yf->size], 0, yarc_padding);

	fclose(yf->fp);
	yf->fp = NULL;

	return 1;
}

bool yarc_file_save(const char* filename, uint8_t* data, size_t size)
{
	FILE* fp = NULL;
	bool success = true;

	if (!filename || !data)
		return false;

	fp = fopen(filename, "wb");

	if (!fp)
		return false;

	if (fwrite(data, 1, size, fp) != size)
	{
		success = false;
	}

	fclose(fp);
	return success;
}


int yarc_file_open(yarc_file_t* yf, bool write)
{
	if (write)
	{
		yf->fp = fopen(yf->filename, "w+b");

		if (!yf->fp)
			return -1;

		return 1;
	}

	yf->fp = fopen(yf->filename, "rb");

	if (!yf->fp)
		return -1;

	fseek(yf->fp, 0, SEEK_END);
	yf->size = (int) ftell(yf->fp);
	fseek(yf->fp, 0, SEEK_SET);

	yf->data = (uint8_t*) malloc(yf->size + yarc_padding);

	if (!yf->data)
		return -1;

	if (fread(yf->data, 1, yf->size, yf->fp) != yf->size)
		return -1;

	memset(&yf->data[yf->size], 0, yarc_padding);

	fclose(yf->fp);
	yf->fp = NULL;

	return 1;
}

void yarc_file_close(yarc_file_t* yf)
{
	if (yf->fp)
	{
		fclose(yf->fp);
		yf->fp = NULL;
	}

	if (yf->filename)
	{
		free(yf->filename);
		yf->filename = NULL;
	}

	if (yf->basename)
	{
		free(yf->basename);
		yf->basename = NULL;
	}

	if (yf->identifier)
	{
		free(yf->identifier);
		yf->identifier = NULL;
	}
}

char* yarc_string_load(const char* filename)
{
	yarc_file_t list;

	memset(&list, 0, sizeof(yarc_file_t));
	list.filename = yarc_list;
	yarc_file_load(&list);
	list.filename = NULL;
	yarc_file_close(&list);

	return (char*) list.data;
}

int yarc_split_by_char(const char* str, const char sep, char*** plines)
{
	int index = 0;
	int count = 0;
	char* p;
	char* end;
	int asize;
	char* line;
	char** lines;
	char* buffer;
	size_t length;

	if (!str || !plines)
		return -1;

	length = strlen(str);

	p = (char*) str;
	end = (char*) &str[length - 1];

	while (p < end)
	{
		if (p[0] == sep)
		{
			count++;
			p += 1;
		}
		else
		{
			p++;
		}
	}

	count++;

	asize = (count + 1) * sizeof(char*);
	buffer = malloc(asize + length + 1);

	if (!buffer)
		return -1;

	lines = (char**) buffer;
	lines[count] = NULL;
	p = &buffer[asize];
	p[length] = '\0';

	memcpy(p, str, length);
	end = &p[length - 1];
	line = p;

	while (p < end)
	{
		if (p[0] == sep)
		{
			p[0] = '\0';
			p += 1;
			lines[index++] = line;
			line = p;
		}
		else
		{
			p++;
		}
	}

	lines[index++] = line;
	*plines = lines;

	if (line)
	{
		/* quick hack to strip separator from last line */

		length = strlen(line);

		if (length > 1)
		{
			if (line[length - 1] == sep)
				line[length - 1] = '\0';
		}
	}

	return count;
}

int yarc_extract_block(const char* filename)
{
	int status;
	uint8_t* data;
	uint32_t size;
	uint32_t index;
	uint32_t count;
	const char* name;
	yarc_file_t file;
	yarc_block_t* block;

	memset(&file, 0, sizeof(yarc_file_t));
	file.filename = (char*) filename;

	status = yarc_file_load(&file);

	if (status < 1)
		return status;

	block = yarc_block_open(file.data, file.size);

	yarc_file_close(&file);

	if (!block)
		return -1;

	printf("%s\n", yarc_block_name(block));

	count = yarc_block_count(block);

	for (index = 0; index < count; index++)
	{
		data = (uint8_t*) yarc_block_entry(block, index, &size, &name);
		printf("%s (%d bytes)\n", name, size);
		yarc_file_save(name, data, size);
	}

	yarc_block_close(block);

	return 1;
}

void yarc_print_help()
{
	printf(
		"YARC: Yet Another Resource Compiler\n"
		"\n"
		"Usage:\n"
		"    yarc [options] <input files>\n"
		"\n"
		"Options:\n"
		"    -o <output file>  output file (default is \"resources.c\")\n"
		"    -l <input list>   input list file (one file per line)\n"
		"    -p <prefix>       name prefix (default is \"yarc\")\n"
		"    -b <bundle>       bundle name (default is \"default\")\n"
		"    -w <width>        hex dump width (default is 16)\n"
		"    -a <padding>      append zero padding (default is 2)\n"
		"    -u                use uppercase hex (default is lowercase)\n"
		"    -s                use static keyword on resources\n"
		"    -z                use bundle compression (lz4)\n"
		"    -k                use block format\n"
		"    -e                extract block\n"
		"    -h                print help\n"
		"    -v                print version (%s)\n"
		"    -V                verbose mode\n"
		"\n", yarc_version
	);
}

void yarc_print_version()
{
	printf("yarc version %s\n", yarc_version);
}

int main(int argc, char** argv)
{
	int index = 0;
	int status = 0;
	int zstatus = 0;
	char* arg = NULL;
	size_t size = 0;
	uint8_t* data = NULL;
	size_t zsize = 0;
	uint8_t* zdata = NULL;
	size_t offset = 0;
	int nfiles = 0;
	yarc_file_t* out = NULL;
	yarc_file_t* file = NULL;
	yarc_file_t* files = NULL;

	if (argc < 2)
	{
		yarc_print_help();
		return 1;
	}

	files = (yarc_file_t*) calloc(argc, sizeof(yarc_file_t));

	if (!files)
		return 1;

	for (index = 1; index < argc; index++)
	{
		arg = argv[index];

		if ((strlen(arg) == 2) && (arg[0] == '-'))
		{
			switch (arg[1])
			{
				case 'o':
					if ((index + 1) < argc)
					{
						yarc_output = argv[index + 1];
						index++;
					}
					break;

				case 'l':
					if ((index + 1) < argc)
					{
						yarc_list = argv[index + 1];
						index++;
					}
					break;

				case 'p':
					if ((index + 1) < argc)
					{
						yarc_prefix = argv[index + 1];
						index++;
					}
					break;

				case 'b':
					if ((index + 1) < argc)
					{
						yarc_bundle = argv[index + 1];
						index++;
					}
					break;

				case 'w':
					if ((index + 1) < argc)
					{
						yarc_width = atoi(argv[index + 1]);
						index++;
					}
					break;

				case 'd':
					if ((index + 1) < argc)
					{
						yarc_padding = atoi(argv[index + 1]);
						index++;
					}
					break;

				case 'u':
					yarc_upper = true;
					break;

				case 's':
					yarc_static = true;
					break;

				case 'z':
					yarc_compress = true;
					break;

				case 'k':
					yarc_block = true;
					break;

				case 'e':
					yarc_extract = true;
					break;

				case 'h':
					yarc_print_help();
					break;

				case 'v':
					yarc_print_version();
					break;

				case 'V':
					yarc_verbose = true;
					break;
			}

			continue;
		}

		if (yarc_block && !strcmp(yarc_output, "resources.c"))
			yarc_output = "resources.yarc";

		if (!yarc_list)
		{
			file = &files[nfiles++];
			file->filename = _strdup(argv[index]);
			yarc_file_construct_names(file);
		}
	}

	if (yarc_list)
	{
		int index;
		int count;
		int length;
		char* input;
		char* line = NULL;
		char** lines = NULL;

		input = yarc_string_load(yarc_list);

		if (!input)
		{
			fprintf(stderr, "could not load input list file: %s\n", yarc_list);
			return 1;
		}

		count = yarc_split_by_char(input, '\n', &lines);
		free(input);

		nfiles = 0;
		free(files);

		files = (yarc_file_t*) calloc(count + 1, sizeof(yarc_file_t));

		for (index = 0; index < count; index++)
		{
			line = lines[index];
			length = strlen(line);

			if (line[length - 1] == '\r')
				line[length - 1] = '\0';

			file = &files[nfiles++];
			file->filename = _strdup(line);
			yarc_file_construct_names(file);
		}

		free(lines);
	}

#ifndef YARC_LZ4
	yarc_compress = false;
#endif

	if (yarc_verbose)
	{
		printf("generating %s from ", yarc_output);

		for (index = 0; index < nfiles; index++)
		{
			file = &files[index];
			printf("%s%s", file->basename, index != (nfiles - 1) ? ", " : "\n");
		}
	}

	if (yarc_extract && (nfiles > 0))
	{
		yarc_extract_block(files[0].filename);
		return 1;
	}

	out = &files[nfiles];
	out->filename = _strdup(yarc_output);
	status = yarc_file_open(out, true);

	if (status < 1)
	{
		fprintf(stderr, "could not open file \"%s\"\n", out->filename);
		return 1;
	}

	offset = 0;

	for (index = 0; index < nfiles; index++)
	{
		file = &files[index];

		status = yarc_file_load(file);

		if (status < 1)
		{
			fprintf(stderr, "could not open file \"%s\"\n", file->filename);
			return 1;
		}

		file->offset = offset;
		offset += file->size + yarc_padding;
	}

	size = offset;

	if (yarc_compress)
	{
		offset = 0;
		zsize = size;

		data = (uint8_t*) malloc(size);
		zdata = (uint8_t*) malloc(size);

		if (!data || !zdata)
			return 1;

		for (index = 0; index < nfiles; index++)
		{
			file = &files[index];
			memcpy(&data[file->offset], file->data, file->size + yarc_padding);
		}

		zstatus = LZ4_compress_HC((const char*) data, (char*) zdata, (int) size, (int) zsize, 12);

		if (zstatus < 1)
		{
			/* compression failed or did not result in a gain, use uncompressed data instead */
			yarc_compress = false;
			free(zdata);
			zdata = NULL;
		}

		zsize = zstatus;
	}

	if (yarc_verbose)
	{
		if (yarc_compress)
			printf("bundle compression is used: %d/%d = %f\n", (int) zsize, (int) size, (float) zsize / (float) size);
		else
			printf("bundle compression is not used\n");
	}

	/* start generating resource bundle file */

	if (yarc_block)
	{
		uint8_t* ptr;
		size_t blockSize;
		uint8_t* blockData;

		blockSize = 24 + yarc_string_size(yarc_bundle);

		for (index = 0; index < nfiles; index++)
		{
			file = &files[index];
			blockSize += 8 + yarc_string_size(file->basename);
		}

		blockSize = (blockSize + 15) & ~0xF;

		blockData = (uint8_t*) malloc(blockSize);

		if (!blockData)
			return 1;

		ptr = blockData;

		*((uint32_t*) &ptr[0]) = YARC_MAGIC;		/* magic, 0x43524159, "YARC" */
		*((uint32_t*) &ptr[4]) = (uint32_t) blockSize;	/* payload data offset */
		*((uint32_t*) &ptr[8]) = (uint32_t) size;	/* uncompressed data size */
		*((uint32_t*) &ptr[12]) = (uint32_t) zsize;	/* compressed data size */
		*((uint32_t*) &ptr[16]) = (uint32_t) nfiles;	/* number of resources */
		*((uint32_t*) &ptr[20]) = (uint32_t) 0;		/* flags (reserved) */
		ptr += 24;

		ptr += yarc_string_write(ptr, yarc_bundle);

		for (index = 0; index < nfiles; index++)
		{
			file = &files[index];

			*((uint32_t*) &ptr[0]) = (uint32_t) file->offset;
			*((uint32_t*) &ptr[4]) = (uint32_t) file->size;
			ptr += 8;

			ptr += yarc_string_write(ptr, file->basename);
		}

		memset(ptr, 0, blockSize - (ptr - blockData));

		if (fwrite(blockData, 1, blockSize, out->fp) != blockSize)
			return 1;

		if (yarc_compress)
		{
			if (fwrite(zdata, 1, zsize, out->fp) != zsize)
				return 1;
		}
		else
		{
			if (fwrite(data, 1, size, out->fp) != size)
				return 1;
		}

		free(blockData);
	}
	else
	{
		fprintf(out->fp, "\n");

		for (index = 0; index < nfiles; index++)
		{
			file = &files[index];

			status = yarc_file_write(out, file);

			if (status < 1)
				return 1;
		}

		if (yarc_compress)
		{
			status = yarc_zdata_write(out, zdata, zsize, size);

			if (status < 1)
				return 1;
		}

		fprintf(out->fp,
			"typedef struct {\n"
			"  const char* name;\n"
			"  const unsigned int* size;\n"
			"  const unsigned char** data;\n"
			"  const unsigned int offset;\n"
			"} %s_resource_t;\n\n", yarc_prefix);

		fprintf(out->fp,
			"typedef struct {\n"
			"  const char* name;\n"
			"  const unsigned int size;\n"
			"  const unsigned char* data;\n"
			"  const unsigned int zsize;\n"
			"  const unsigned char* zdata;\n"
			"  const %s_resource_t* resources;\n"
			"} %s_bundle_t;\n\n", yarc_prefix, yarc_prefix);

		fprintf(out->fp, "%sconst %s_resource_t %s_%s_resources[] = {\n",
			yarc_static ? "static " : "",
			yarc_prefix, yarc_prefix, yarc_bundle);

		for (index = 0; index < nfiles; index++)
		{
			file = &files[index];
			fprintf(out->fp, "  { \"%s\", &%s_size, &%s_data, %d },\n",
				file->basename, file->identifier, file->identifier, (int) file->offset);
		}

		fprintf(out->fp, "  { \"\", 0, 0, 0 }\n};\n");
		fprintf(out->fp, "\n");

		fprintf(out->fp, "%s%s_bundle_t %s_%s_bundle = {\n",
			yarc_static ? "static " : "",
			yarc_prefix, yarc_prefix, yarc_bundle);

		if (yarc_compress)
		{
			fprintf(out->fp, "  \"%s\", %d, 0, %d, %s_%s_zdata, %s_%s_resources\n",
				yarc_bundle, (int) size, (int) zsize, yarc_prefix, yarc_bundle, yarc_prefix, yarc_bundle);
		}
		else
		{
			fprintf(out->fp, "  \"%s\", %d, 0, %d, 0, %s_%s_resources\n",
				yarc_bundle, (int) size, (int) zsize, yarc_prefix, yarc_bundle);
		}

		fprintf(out->fp, "};\n");
		fprintf(out->fp, "\n");
	}

	yarc_file_close(out);
	free(files);

	free(data);
	free(zdata);

	return 0;
}

