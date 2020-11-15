#define TM_SYMBOL_TREE_GROWTH 32

typedef struct tm_symbol_node_t
{
	uint64_t hash;
	uint64_t byte_offset;
	uint32_t string_length;
	TM_PAD(4);
	uint32_t left;
	uint32_t right;
} tm_symbol_node_t;

typedef struct tm_symbol_tree_t
{
	tm_symbol_node_t *nodes;
	uint32_t node_count;
	uint32_t node_capacity;
} tm_symbol_tree_t;

static inline uint32_t tm_symbol_tree_insert_internal(tm_symbol_tree_t *tree, uint32_t root_idx, uint32_t node_idx)
{
	if (root_idx == node_idx)
		return node_idx;

	tm_symbol_node_t *root = tree->nodes + root_idx;
	const uint64_t hash = tree->nodes[node_idx].hash;

	if (hash < root->hash) {
		root->left = root->left ? tm_symbol_tree_insert_internal(tree, root->left, node_idx) : node_idx;
	}
	else if (hash > root->hash) {
		root->right = root->right ? tm_symbol_tree_insert_internal(tree, root->right, node_idx) : node_idx;
	}

	return root_idx;
}

static inline void tm_symbol_tree_insert(tm_allocator_i *a, tm_symbol_tree_t *tree, uint64_t hash, uint64_t offset, uint32_t string_length)
{
	if (tree->node_count >= tree->node_capacity) {
		const size_t old_byte_size = tree->node_capacity * sizeof(tm_symbol_node_t);
		tree->node_capacity += TM_SYMBOL_TREE_GROWTH;
		const size_t new_byte_size = tree->node_capacity * sizeof(tm_symbol_node_t);
		tree->nodes = tm_realloc(a, tree->nodes, old_byte_size, new_byte_size);
	}

	const uint32_t i = tree->node_count++;
	tree->nodes[i] = (tm_symbol_node_t){
		.hash = hash,
		.byte_offset = offset,
		.string_length = string_length
	};

	tm_symbol_tree_insert_internal(tree, 0, i);
}

static inline bool tm_symbol_tree_contains_internal(const tm_symbol_tree_t *tree, uint32_t root_idx, uint64_t hash)
{
	const tm_symbol_node_t *root = tree->nodes + root_idx;
	if (root->hash == hash)
		return true;

	if (hash < root->hash && root->left)
		return tm_symbol_tree_contains_internal(tree, root->left, hash);
	else if (hash > root->hash && root->right)
		return tm_symbol_tree_contains_internal(tree, root->right, hash);

	return false;
}

static inline bool tm_symbol_tree_contains(const tm_symbol_tree_t *tree, uint64_t hash)
{
	return tree->nodes ? tm_symbol_tree_contains_internal(tree, 0, hash) : false;
}

static inline bool tm_symbol_tree_search_internal(const tm_symbol_tree_t *tree, uint32_t root_idx, uint64_t hash, uint32_t *result_idx)
{
	const tm_symbol_node_t *root = tree->nodes + root_idx;
	if (root->hash == hash) {
		*result_idx = root_idx;
		return true;
	}

	if (hash < root->hash && root->left)
		return tm_symbol_tree_search_internal(tree, root->left, hash, result_idx);
	else if (hash > root->hash && root->right)
		return tm_symbol_tree_search_internal(tree, root->right, hash, result_idx);

	return false;
}

static inline bool tm_symbol_tree_search(const tm_symbol_tree_t *tree, uint64_t hash, uint32_t *result_idx)
{
	return tree->nodes ? tm_symbol_tree_search_internal(tree, 0, hash, result_idx) : false;
}

static inline void tm_symbol_tree_free(tm_allocator_i *a, tm_symbol_tree_t *tree)
{
	const uint32_t size = (tree->node_capacity > tree->node_count ? tree->node_capacity : tree->node_count) * sizeof(tm_symbol_node_t);
	tm_free(a, tree->nodes, size);
}