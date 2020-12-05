#include <foundation/allocator.h>
#include <foundation/carray.inl>

typedef struct tm_huffman_node_t
{
	char data;
	TM_PAD(3);
	uint32_t left;
	uint32_t right;
} tm_huffman_node_t;

typedef struct tm_huffman_tree_t
{
	uint32_t node_count;
	TM_PAD(4);
	tm_huffman_node_t *nodes;
	uint32_t code_lut[0xFF];
} tm_huffman_tree_t;

#define tm_huffman_code__bit_count(code)	((code) >> 24)
#define tm_huffman_code__code_word(code)	((code) & 0x00FFFFFF)

typedef struct private__huffman_heap_node_t
{
	uint8_t data;
	TM_PAD(3);
	uint32_t frequency;
	struct private__huffman_heap_node_t *left;
	struct private__huffman_heap_node_t *right;
} private__huffman_heap_node_t;

typedef struct private__huffman_heap_t
{
	uint32_t size;
	uint32_t capacity;
	private__huffman_heap_node_t **data;
} private__huffman_heap_t;

static inline void private__huffman_heapify(private__huffman_heap_t *heap, uint32_t idx)
{
	uint32_t smallest = idx;
	const uint32_t left = (idx << 1) + 1;
	const uint32_t right = (idx << 1) + 2;

	if (left < heap->size && heap->data[left]->frequency < heap->data[smallest]->frequency)
		smallest = left;
	if (right < heap->size && heap->data[right]->frequency < heap->data[smallest]->frequency)
		smallest = right;

	if (smallest != idx) {
		private__huffman_heap_node_t *tmp = heap->data[smallest];
		heap->data[smallest] = heap->data[idx];
		heap->data[idx] = tmp;

		private__huffman_heapify(heap, smallest);
	}
}

static inline private__huffman_heap_node_t *private__huffman_get_min(private__huffman_heap_t *heap)
{
	private__huffman_heap_node_t *result = *heap->data;
	*heap->data = heap->data[--heap->size];
	private__huffman_heapify(heap, 0);
	return result;
}

static inline private__huffman_heap_node_t *private__huffman_node_create(tm_temp_allocator_i *ta, uint8_t data, uint32_t frequency)
{
	private__huffman_heap_node_t *result = tm_temp_alloc(ta, sizeof(private__huffman_heap_node_t));
	*result = (private__huffman_heap_node_t) { .data = data, .frequency = frequency };
	return result;
}

static inline uint32_t private__huffman_convert_heap_to_tree(tm_huffman_tree_t *tree, const private__huffman_heap_node_t *root, uint32_t *idx)
{
	const uint32_t i = (*idx)++;

	tree->nodes[i] = (tm_huffman_node_t) {
		.data = root->data,
		.left = root->left ? private__huffman_convert_heap_to_tree(tree, root->left, idx) : 0,
		.right = root->right ? private__huffman_convert_heap_to_tree(tree, root->right, idx) : 0
	};

	return i;
}

static inline void private__huffman_set_code_word(tm_huffman_tree_t *tree, const private__huffman_heap_node_t *root, uint32_t depth, uint32_t code)
{
	if (root->data)
		tree->code_lut[root->data] = ((depth - 1) << 24) | code;

	if (root->left)
		private__huffman_set_code_word(tree, root->left, depth + 1, code);

	if (root->right)
		private__huffman_set_code_word(tree, root->right, depth + 1, code | (1 << (depth - 1)));
}

static inline tm_huffman_tree_t tm_huffman_tree_create(tm_allocator_i *a, const char **strings)
{
	TM_INIT_TEMP_ALLOCATOR(ta);
	const uint32_t string_count = (uint32_t)tm_carray_size(strings);
	if (string_count == 0)
		return (tm_huffman_tree_t) { 0 };

	// Calculate character frequency.
	// This needs to be done with unsigned strings in order to handle unicode characters.
	uint32_t frequencies[0xFF] = { 0 };
	for (uint32_t i = 0, j = 0; i < string_count; ++i, j = 0) {
		const uint8_t *uni_str = (const uint8_t *)strings[i];
		for (uint8_t c = *uni_str; c != '\0'; c = uni_str[++j])
			++frequencies[c];
	}

	// Create temporary heap.
	private__huffman_heap_t heap = { 0 };
	for (uint32_t i = 0; i < TM_ARRAY_COUNT(frequencies); ++i)
		heap.capacity += frequencies[i] > 0;

	heap.size = heap.capacity;
	heap.data = tm_temp_alloc(ta, heap.capacity * sizeof(private__huffman_heap_node_t *));
	for (uint32_t i = 0, j = 0; i < TM_ARRAY_COUNT(frequencies); ++i) {
		if (frequencies[i])
			heap.data[j++] = private__huffman_node_create(ta, (uint8_t)i, frequencies[i]);
	}

	// Build heap.
	for (int32_t i = (heap.size - 2) >> 1; i >= 0; --i)
		private__huffman_heapify(&heap, i);

	while (heap.size != 1) {
		private__huffman_heap_node_t *left = private__huffman_get_min(&heap);
		private__huffman_heap_node_t *right = private__huffman_get_min(&heap);
		private__huffman_heap_node_t *top = private__huffman_node_create(ta, '\0', left->frequency + right->frequency);

		top->left = left;
		top->right = right;

		uint32_t i = heap.size++;
		while (i && top->frequency < heap.data[(i - 1) >> 1]->frequency) {
			heap.data[i] = heap.data[(i - 1) >> 1];
			i = (i - 1) >> 1;
		}

		heap.data[i] = top;
	}

	uint32_t i = 0;
	const private__huffman_heap_node_t *root = private__huffman_get_min(&heap);

	// Create result tree and lookup.
	tm_huffman_tree_t tree = { .node_count = (heap.capacity << 1) - 1 };
	tree.nodes = tm_alloc(a, tree.node_count * sizeof(tm_huffman_node_t));
	private__huffman_convert_heap_to_tree(&tree, root, &i);
	private__huffman_set_code_word(&tree, root, 1, 0);

	TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
	return tree;
}

static inline char tm_huffman_tree_decode(const tm_huffman_tree_t *tree, const char *src, uint64_t *bit_offset)
{
	uint32_t cur_idx = 0;
	for (;;)
	{
		const tm_huffman_node_t *node = tree->nodes + cur_idx;

		if (node->data)
			return node->data;

		cur_idx = tm_binary_handler_read_single_bit(src, bit_offset) ? node->right : node->left;
	}

	return 0;
}

static inline void tm_huffman_tree_free(tm_allocator_i *a, tm_huffman_tree_t *tree)
{
	tm_free(a, tree->nodes, tree->node_count * sizeof(tm_huffman_node_t));
}

static inline void private__tree_node_dump(const tm_huffman_tree_t *tree, uint32_t root, char *buffer, uint32_t bit)
{
	const tm_huffman_node_t *node = tree->nodes + root;

	if (node->data != '\0') {
		buffer[bit] = '\0';

		// Print control characters in their hex form, these are split up unicode characters.
		if (node->data >= ' ' && node->data <= '~')
			tm_logger_api->printf(TM_LOG_TYPE_DEBUG, "%c: %s\n", node->data, buffer);
		else
			tm_logger_api->printf(TM_LOG_TYPE_DEBUG, "0x%X: %s\n", node->data & 0xFF, buffer);
	}
	else {
		buffer[bit] = '0';
		private__tree_node_dump(tree, node->left, buffer, bit + 1);
		buffer[bit] = '1';
		private__tree_node_dump(tree, node->right, buffer, bit + 1);
	}
}

static inline void tm_huffman_tree_dump(const tm_huffman_tree_t *tree)
{
	if (tree->node_count) {
		char buffer[0xFF];
		private__tree_node_dump(tree, 0, buffer, 0);
	}
}