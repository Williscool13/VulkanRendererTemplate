#include "vk_types.h"

BoundingSphere::BoundingSphere(const RawMeshData& meshData) : center(0.0f, 0.0f, 0.0f), radius(0.0f)
{
	const std::vector<MultiDrawVertex>& _vs = meshData.vertices;
	this->center = { 0, 0, 0 };

	for (auto&& vertex : _vs)
	{
		this->center += vertex.position;
	}
	this->center /= static_cast<float>(_vs.size());
	this->radius = glm::distance2(_vs[0].position, this->center);
	for (size_t i = 1; i < _vs.size(); ++i)
	{
		this->radius = std::max(this->radius, glm::distance2(_vs[i].position, this->center));
	}
	this->radius = std::nextafter(sqrtf(this->radius), std::numeric_limits<float>::max());
}
