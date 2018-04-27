
#include <dyna_cpp/db/DB_Nodes.hpp>
#include <dyna_cpp/db/FEMFile.hpp>
#include <dyna_cpp/db/Node.hpp>
#include <dyna_cpp/db/Part.hpp>
#include <dyna_cpp/utility/TextUtility.hpp>

#include <iterator>
#include <set>

namespace qd {

/**
 * Constructor
 */
Part::Part(int32_t _partID, const std::string& _partName, FEMFile* _femfile)
  : partID(_partID)
  , femfile(_femfile)
  , partName(trim_copy(_partName))
{}

/**
 * Assign a part name.
 */
void
Part::set_name(const std::string& _name)
{
  std::lock_guard<std::mutex> lock(_part_mutex);
  this->partName = trim_copy(_name);
}

/**
 * Get the id of the part.
 */
int32_t
Part::get_partID() const
{
  return this->partID;
}

/** Get the name of the part.
 *
 * @return name
 */
std::string
Part::get_name() const
{
  return this->partName;
}

/**  Add a node to a part.
 *
 * @param _element
 */
void
Part::add_element(std::shared_ptr<Element> _element)
{
  std::lock_guard<std::mutex> lock(_part_mutex);
  this->elements.push_back(_element);
}

/**
 * Get the nodes of the part.
 */
std::vector<std::shared_ptr<Node>>
Part::get_nodes()
{
  std::vector<std::shared_ptr<Node>> nodes;
  std::set<size_t> unique_node_indexes;

  // extract unique indexes
  for (auto& elem : elements) {
    auto elem_node_indexes = elem->get_node_indexes();
    std::copy(elem_node_indexes.begin(),
              elem_node_indexes.end(),
              std::inserter(unique_node_indexes, unique_node_indexes.end()));
  }

  // fetch nodes
  DB_Nodes* db_nodes = this->femfile->get_db_nodes();
  for (const auto node_index : unique_node_indexes) {
    nodes.push_back(db_nodes->get_nodeByIndex(node_index));
  }

  return std::move(nodes);
}

/** Get the elements of the part.
 * @param Element::ElementType : optional filter
 * @return std::vector<Element*> elems
 */
std::vector<std::shared_ptr<Element>>
Part::get_elements(Element::ElementType _etype)
{
  if (_etype == Element::NONE) {
    return this->elements;

  } else {
    std::shared_ptr<Element> tmp_elem = nullptr;
    std::vector<std::shared_ptr<Element>> _elems;

    for (auto& tmp_elem : elements) {
      if (tmp_elem->get_elementType() == _etype) {
        _elems.push_back(tmp_elem);
      }
    }

    return _elems;
  }
}

/** Remove an element
 *
 * @param _element : element to remove
 *
 * Does nothing if element not referenced
 */
void
Part::remove_element(std::shared_ptr<Element> _element)
{
  std::lock_guard<std::mutex> lock(_part_mutex);
  elements.erase(
    std::remove_if(elements.begin(),
                   elements.end(),
                   [_element](auto elem) { return elem == _element; }),
    elements.end());
}

/** Get the node ids of the elements
 *
 * @param element_type
 * @param nNodes : number of nodes (e.g. 3 for tria)
 * @return ids
 */
std::shared_ptr<Tensor<int32_t>>
Part::get_element_node_ids(Element::ElementType element_type, size_t nNodes)
{
  // allocate
  auto tensor = std::make_shared<Tensor<int32_t>>();
  tensor->resize({ elements.size(), nNodes });
  auto& tensor_data = tensor->get_data();

  // copy
  size_t iElement = 0;
  for (auto& element : elements) {

    if (element->get_elementType() == element_type &&
        element->get_nNodes() == nNodes) {
      const auto& elem_node_ids = element->get_node_ids();
      std::copy(elem_node_ids.begin(),
                elem_node_ids.end(),
                tensor_data->begin() + iElement++ * nNodes);
    }
  }

  // resize
  tensor->resize({ iElement, nNodes });

  return tensor;
}

/** Get the indexes of the parts elements
 *
 * @param element_type
 * @param nNodes : number of nodes (e.g. 3 for tria)
 * @return indexes
 */
std::shared_ptr<Tensor<int32_t>>
Part::get_element_node_indexes(Element::ElementType element_type,
                               size_t nNodes) const
{
  auto db_nodes = femfile->get_db_nodes();

  // allocate
  auto tensor = std::make_shared<Tensor<int32_t>>();
  tensor->resize({ elements.size(), nNodes });
  auto& tensor_data = tensor->get_data();

  // copy
  size_t iEntry = 0;
  for (auto& element : elements) {

    if (element->get_elementType() == element_type &&
        element->get_nNodes() == nNodes) {
      const auto& elem_node_ids = element->get_node_ids();
      for (auto id : elem_node_ids)
        tensor_data->operator[](iEntry++) = db_nodes->get_index_from_id(id);
    }
  }

  // resize
  tensor->resize({ iEntry / nNodes, nNodes });

  return std::move(tensor);
}

} // namespace qd