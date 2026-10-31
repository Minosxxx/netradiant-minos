/*
   Read access to a loaded model's triangles (for exporters).

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include "generic/constant.h"
#include <functional>

/// Implemented by model nodes. Coordinates are in the model's local space.
class ModelGeometry
{
public:
	STRING_CONSTANT( Name, "ModelGeometry" );

	struct Vertex
	{
		float xyz[3];
		float normal[3];
		float st[2];
	};
	/// Called once per triangle (winding as stored; use the normals for facing), with the surface's shader name.
	using TriangleVisitor = std::function<void( const char* shader, const Vertex& a, const Vertex& b, const Vertex& c )>;

	virtual void forEachTriangle( const TriangleVisitor& visitor ) const = 0;
};
