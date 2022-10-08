#include <iostream>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stack>
#include <vector>

template <typename T, typename K>
class Node {
   private:
    class SplitResult {
       public:
        explicit SplitResult(Node* left, Node* right, T promoted_key)
            : left_{left}, right_{right}, promoted_key_{promoted_key} {}
        explicit SplitResult(Node* left, Node* right, Node* new_root,
                             T promoted_key)
            : left_{left},
              right_{right},
              new_root_{new_root},
              promoted_key_{promoted_key} {}
        ~SplitResult() = default;
        inline auto GetLeft() -> Node* { return left_; }
        inline auto GetRight() -> Node* { return right_; }
        inline auto HasRoot() -> bool { return new_root_ != nullptr; }
        inline auto GetRoot() -> Node* { return new_root_; }

       private:
        Node* left_;
        Node* right_;
        Node* new_root_;
        T promoted_key_;
    };

   public:
    explicit Node(int min_order)
        : leaf_{true},
          root_{false},
          min_order_{min_order},
          right_link_{nullptr},
          out_link_{nullptr} {}

    explicit Node(int min_order, std::vector<T>& keys)
        : leaf_{true},
          root_{false},
          min_order_{min_order},
          keys_{keys},
          right_link_{nullptr},
          out_link_{nullptr} {}

    explicit Node(int min_order, std::vector<T>& keys, std::vector<K>& children)
        : leaf_{true},
          root_{false},
          min_order_{min_order},
          keys_{keys},
          children_{children},
          right_link_{nullptr},
          out_link_{nullptr} {}

    ~Node() = default;

    /**
     * Unsafely latch this node
     */
    inline void Latch() { latch_.lock(); }

    /**
     * Unsafely unlatch this node
     */
    inline void Unlatch() { latch_.unlock(); }

    /**
     * Return the keys of this node
     */
    inline auto GetKeys() -> std::vector<T> { return keys_; }

    /**
     * Return the children of this node
     */
    inline auto GetChildren() -> std::vector<K> { return children_; }

    /**
     * Whether this node is the root or not
     */
    inline auto IsRoot() -> bool { return root_; }

    /**
     * Whether this node is a leaf node
     */
    inline auto IsLeaf() -> bool { return leaf_; }

    /**
     * Set/unset whether this node is the root or not
     */
    inline void SetRoot(bool root) { root_ = root; }

    /**
     * Set/unset whether this node is a leaf node or not
     */
    inline void SetLeaf(bool leaf) { leaf_ = leaf; }

    /**
     * Set the keys of this node
     */
    inline void SetKeys(std::vector<T> keys) { keys_ = keys; }

    /**
     * Set the children of this node
     */
    inline void SetChildren(std::vector<K> children) { children_ = children; }

    /**
     * Set the right link of this node
     */
    inline void SetRight(Node* right_link) { right_link_ = right_link; };

    /**
     * Set the out link of this node
     */
    inline void SetOut(Node* out_link) { out_link_ = out_link; };

    /**
     * Find the index at which this key exists, or return the index at which
     * this key should be inserted at
     * - Uses binary search
     * - Requires operator== to be overloaded
     */
    auto FindIndex(const T& key) -> int {
        auto start = 0;
        auto end = keys_.size() - 1;
        while (start <= end) {
            auto mid = (start + end) / 2;
            if (keys_[mid] == key)
                return mid;
            else if (keys_[mid] < key)
                start = mid + 1;
            else
                end = mid - 1;
        }
        return end + 1;
    }

    /**
     * Split a node into two halves safely. Update the required right_link_
     * fields.
     */
    auto Split() -> SplitResult* {
        // RAII lock guard
        std::lock_guard<std::mutex> lk{latch_};
        // split keys
        auto [left_half_keys, right_half_keys] = SplitVec<T>(keys_);
        auto promoted_key = left_half_keys.back();
        // create new right sibling
        auto new_right = new Node<T, K>(min_order_, right_half_keys);
        // set the new right sibling's right_link_ field to the current node's
        // right_link_ field
        new_right->SetRight(right_link_);
        // set the current node's right_link field to point to the new right
        // sibling
        right_link_ = new_right;
        new_right->SetKeys(right_half_keys);
        // update current node keys
        SetKeys(left_half_keys);
        new_right->SetLeaf(leaf_);
        // init potential new root
        Node* new_root = nullptr;
        if (root_) {
            // if the current node is the root, create a new one and set the
            // proper key and children
            new_root = new Node<T, K>(min_order_);
            auto new_root_keys = std::vector<int>{promoted_key};
            auto new_root_children = std::vector<void*>{this, new_right};
            new_root->SetKeys(new_root_keys);
            new_root->SetChildren(new_root_children);
            new_root->SetRoot(true);
        }
        if (leaf_)
            return new SplitResult(this, new_right, new_root, promoted_key);
        // if the node's were internal nodes, split the children as well
        auto [left_half_children, right_half_children] = SplitVec<K>(children_);
        SetChildren(left_half_children);
        new_right->SetChildren(right_half_children);
        // lock guard is released at the end of this scope
        return new SplitResult(this, new_right, new_root, promoted_key);
    }

    /**
     * If this node is safe, insert the key
     */
    auto InsertSafe(const T& key) -> bool {
        // quick check
        if (!IsSafe()) return false;
        // if this node already contains the key return
        if (Contains(key)) return false;
        // otherwise insert and return true
        auto insert_at = FindIndex(key);
        keys_.insert(keys_.begin() + insert_at, key);
        return true;
    }

    /**
     * Scan the current node and determine whether the bounds of this subtree
     * are acceptable. If not, return right_link_. If so, return the correct
     * (acceptable) child.
     */
    auto Scannode(const T& key) -> K* {
        auto index = FindIndex(key);
        if (index == keys_.size() && right_link_ != nullptr) return right_link_;
        return children_[index];
    }

    /**
     * Whether this node is safe given it's min_order_ property.
     */
    inline auto IsSafe() -> bool {
        return keys_.size() > min_order_ && keys_.size() < 2 * min_order_;
    }

    /**
     * Whether this node contains the key passed in
     */
    inline auto Contains(const T& key) -> bool {
        auto index = FindIndex(key);
        // the passed in key cannot exist in keys_ if the index returned above
        // is equal to the size of keys_
        if (index == keys_.size()) return false;
        return keys_[index] == key;
    }

    /**
     * Move right along a node until one is reached that has appropriate bounds
     * for the key passed in.
     */
    static auto MoveRight(Node* current, const T& key) -> Node* {
        if (current == nullptr) return nullptr;
        // latch the current node for thread safety
        current->Latch();
        // scan the current node for the right link to follow.
        K* t = current->Scannode(key);
        Node* target = nullptr;
        // go right until we reach a node with acceptable bounds or until we are
        // at the rightmost node on the level (where right_link == nullptr
        // evaluates to true)
        while (t == current->right_link_ && current->right_link_ != nullptr) {
            target = static_cast<Node<T, K>*>(t);
            // latch the target node before we unlatch the current mode, then
            // reassign the pointer
            target->Latch();
            current->Unlatch();
            current = target;
        }
        return current;
    }

   private:
    friend auto operator<<(std::ostream& os, const Node& node)
        -> std::ostream& {
        os << "Node {\n\tleaf_: " << node.leaf_ << ",\n\troot_: " << node.root_
           << ",\n\tmin_order_: " << node.min_order_ << ",\n}";
        return os;
    }

    friend auto operator<<(std::ostream& os, const Node* node)
        -> std::ostream& {
        os << "Node {\n\tleaf_: " << node->leaf_
           << ",\n\troot_: " << node->root_
           << ",\n\tmin_order_: " << node->min_order_ << ",\n}";
        return os;
    }

    template <typename R>
    static auto SplitVec(std::vector<R>& vec)
        -> std::tuple<std::vector<R>, std::vector<R>> {
        auto mid = vec.size() % 2 == 0 ? vec.size() / 2 : vec.size() / 2 + 1;
        std::vector<R> low(vec.begin(), vec.begin() + mid);
        std::vector<R> high(vec.begin() + mid, vec.end());
        return std::make_tuple(low, high);
    }

    bool leaf_;
    bool root_;
    int min_order_;
    std::vector<T> keys_;
    std::vector<K> children_;
    std::mutex latch_;
    Node *right_link_, *out_link_;
};

template <typename T, typename K>
class Tree {
   public:
    explicit Tree() : root_{nullptr}, min_order_{2} {}
    explicit Tree(int min_order) : root_{nullptr}, min_order_{min_order} {};
    ~Tree() = default;

    auto Latch() { latch_.lock(); }

    auto Unlatch() { latch_.unlock(); }

    auto Insert(const T& key, const K& val) -> bool {
        // initialize stack
        auto anc_stack = std::stack<Node<T, K>*>{};
        auto current = root_;
        // if the root is null then just create a new node, insert the key, and
        // assign it to this tree's root_ property
        if (current == nullptr) {
            // latch the tree with a lock guard
            std::lock_guard<std::mutex> lk{latch_};
            root_ = new Node<T, K>(min_order_);
            auto keys = std::vector<T>{key};
            root_->SetKeys(keys);
            root_->SetRoot(true);
            // latch is destroyed at the end of this scope
            return true;
        }

        // continue until we hit a leaf
        while (!current->IsLeaf()) {
            auto t = current;
            current = static_cast<Node<T, K>*>(current->Scannode(key));
            // we only want to push the rightmost node at each level onto the
            // stack, so if
            if (current != t->right_link_) anc_stack.push(t);
        }
        current = Node<T, K>::MoveRight(current, key);
        // current is now LATCHED
        if (current->Contains(key)) {
            std::cout << "Key already exists in tree" << std::endl;
            return false;
        }
        // current is ALWAYS latched when the following recursive procedure is
        // called
        return Insert(current, key, val, anc_stack);
    }

   private:
    // recursive insert procedure
    auto Insert(Node<T, K>* current, const T& key, const K& val,
                std::stack<Node<T, K>*> anc_stack) -> bool {
        // current is ALWAYS latched at this state so it should be safe to
        // unlatch it without deadlock
        if (current->IsSafe()) {
            current->InsertSafe(key);
            // unlatch and return. we're done if the node we've reached is safe.
            current->Unlatch();
            return true;
        }
        auto split = current->Split();
        if (split->HasRoot()) {
            std::lock_guard<std::mutex> lk{latch_};
            root_ = split->GetRoot();
            return true;
        }
        auto cast_key = const_cast<T>(key);
        auto old_node = current;
        current = anc_stack.pop();
        current->Latch();
        Node<T, K>::MoveRight(current, cast_key);
        old_node->Unlatch();
        return Insert(current, key, val, anc_stack);
    }
    Node<T, K>* root_;
    std::mutex latch_;
    int min_order_;
};

auto main(int argc, char** argv) -> int {
    // auto keys = std::vector<int>{1, 2, 3, 4};
    // auto node = new Node<int, void*>(2, keys);
    std::cout << "hello, world" << std::endl;
}