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
        : min_order_{min_order},
          leaf_{true},
          root_{false},
          right_link_{nullptr},
          out_link_{nullptr} {}

    explicit Node(int min_order, std::vector<T>& keys)
        : min_order_{min_order},
          leaf_{true},
          root_{false},
          right_link_{nullptr},
          out_link_{nullptr},
          keys_{keys} {}

    explicit Node(int min_order, std::vector<T>& keys, std::vector<K>& children)
        : min_order_{min_order},
          leaf_{true},
          root_{false},
          right_link_{nullptr},
          out_link_{nullptr},
          keys_{keys},
          children_{children} {}

    ~Node() = default;

    inline void Latch() { latch_.lock(); }

    inline void Unlatch() { latch_.unlock(); }

    inline auto GetKeys() -> std::vector<T> { return keys_; }

    inline auto GetChildren() -> std::vector<K> { return children_; }

    inline auto IsRoot() -> bool { return root_; }

    inline auto IsLeaf() -> bool { return leaf_; }

    inline void SetRoot(bool root) { root_ = root; }

    inline void SetLeaf(bool leaf) { leaf_ = leaf; }

    inline void SetKeys(std::vector<T> keys) { keys_ = keys; }

    inline void SetChildren(std::vector<K> children) { children_ = children; }

    inline void SetRight(Node* right_link) { right_link_ = right_link; };

    inline void SetOut(Node* out_link) { out_link_ = out_link; };

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

    auto Split() -> SplitResult* {
        std::lock_guard<std::mutex> lk{latch_};
        auto [left_half_keys, right_half_keys] = SplitVec<T>(keys_);
        auto promoted_key = left_half_keys.back();
        auto new_right = new Node<T, K>(min_order_, right_half_keys);
        new_right->SetRight(right_link_);
        right_link_ = new_right;
        new_right->SetKeys(right_half_keys);
        SetKeys(left_half_keys);
        Node* new_root = nullptr;
        if (root_) {
            new_root = new Node<T, K>(min_order_);
            auto new_root_keys = std::vector<int>{promoted_key};
            auto new_root_children = std::vector<void*>{this, new_right};
            new_root->SetKeys(new_root_keys);
            new_root->SetChildren(new_root_children);
        }
        if (leaf_)
            return new SplitResult(this, new_right, new_root, promoted_key);
        auto [left_half_children, right_half_children] = SplitVec<K>(children_);
        SetChildren(left_half_children);
        new_right->SetChildren(right_half_children);
        return new SplitResult(this, new_right, new_root, promoted_key);
    }

    auto InsertSafe(const T& key) -> bool {
        if (Contains(key)) return false;
        auto insert_at = FindIndex(key);
        keys_.insert(keys_.begin() + insert_at, key);
        return true;
    }

    auto Scannode(const T& key) -> K* {
        auto index = FindIndex(key);
        if (index == keys_.size() && right_link_ != nullptr) return right_link_;
        return children_[index];
    }

    inline auto IsSafe() -> bool {
        return keys_.size() > min_order_ && keys_.size() < 2 * min_order_;
    }

    inline auto Contains(const T& key) -> bool {
        auto index = FindIndex(key);
        return keys_[index] == key;
    }

    static auto MoveRight(Node* current, const T& key) -> Node* {
        if (current == nullptr) return nullptr;
        current->Latch();
        void* t = current->Scannode(key);
        Node* target = nullptr;
        while (t == current->right_link_ && current->right_link_ != nullptr) {
            target = static_cast<Node<T, K>*>(t);
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
    Tree() : root_{nullptr} {}
    ~Tree() = default;

    auto Insert(const T& key, const K& val) -> bool {
        auto anc_stack = std::stack<Node<T, K>*>{};
        auto current = root_;
        while (!current->IsLeaf()) {
            auto t = current;
            current = static_cast<Node<T, K>*>(current->Scannode(key));
            if (current != t->right_link_) anc_stack.push(t);
        }
        current = Node<T, K>::MoveRight(current, key);
        if (current->Contains(key)) {
            std::cout << "Key already exists in tree" << std::endl;
            return false;
        }
        return Insert(current, key, val, anc_stack);
    }

   private:
    auto Insert(Node<T, K>* current, const T& key, const K& val,
                std::stack<Node<T, K>*> anc_stack) {
        if (current->IsSafe()) {
            current->InsertSafe(key);
        }
    }
    Node<T, K>* root_;
};

auto main(int argc, char** argv) -> int {
    auto keys = std::vector<int>{1, 2, 3, 4};
    auto node = new Node<int, void*>(2, keys);
    (void)node->Split();
    std::cout << "hello, world" << std::endl;
}