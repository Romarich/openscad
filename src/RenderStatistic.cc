/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2020 Golubev Alexander <fatzer2@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <json.hpp>

#include "printutils.h"
#include "GeometryCache.h"
#include "CGALCache.h"
#include "polyset.h"
#include "Polygon2d.h"
#include "boost-utils.h"
#ifdef ENABLE_CGAL
#include "CGAL_Nef_polyhedron.h"
#endif // ENABLE_CGAL

#include "RenderStatistic.h"
#include "parameter/parameterobject.h"

namespace {

struct StatisticVisitor: public GeometryVisitor
{
	StatisticVisitor(const std::vector<std::string>& options)
		: all(std::find(options.begin(), options.end(), "all") != options.end())
	  , options(options) { }
	virtual void printCamera(const Camera& camera) = 0;
	virtual void printCacheStatistic() = 0;
	virtual void printRenderingTime(std::chrono::milliseconds) = 0;
	virtual void finish() = 0;
protected:
	bool is_enabled(const std::string& name) {
		return all || std::find(options.begin(), options.end(), name) != options.end();
	}
private:
	bool all;
	std::vector<std::string> options;
};

struct LogVisitor: public StatisticVisitor
{
	LogVisitor(const std::vector<std::string>& options) : StatisticVisitor(options) { }
  void visit(const class GeometryList &node) override;
  void visit(const class PolySet &node) override;
  void visit(const class Polygon2d &node) override;
#ifdef ENABLE_CGAL
  void visit(const class CGAL_Nef_polyhedron &node) override;
#endif // ENABLE_CGAL
	void printCamera(const Camera& camera) override;
	void printCacheStatistic() override;
	void printRenderingTime(std::chrono::milliseconds) override;
	void finish() override;
private:
	void printBoundingBox3(const BoundingBox& bb);
};

struct StreamVisitor: public StatisticVisitor
{
	StreamVisitor(const std::vector<std::string>& options, std::ostream &stream) : StatisticVisitor(options), stream(stream) {}
	StreamVisitor(const std::vector<std::string>& options, const std::string& filename) : StatisticVisitor(options), fstream(filename), stream(fstream)	{}
	~StreamVisitor() {
		if (fstream.is_open()) fstream.close();
	}
  void visit(const class GeometryList &node) override;
  void visit(const class PolySet &node) override;
  void visit(const class Polygon2d &node) override;
#ifdef ENABLE_CGAL
  void visit(const class CGAL_Nef_polyhedron &node) override;
#endif // ENABLE_CGAL
	void printCamera(const Camera& camera) override;
	void printCacheStatistic() override;
	void printRenderingTime(std::chrono::milliseconds) override;
	void finish() override;
private:
	nlohmann::json json;
	std::ofstream fstream;
	std::ostream &stream;
};

template <typename G>
static nlohmann::json getBoundingBox2(G geometry)
{
	const auto& bb = geometry.getBoundingBox();
	const std::array<double, 2> min = { bb.min().x(), bb.min().y() };
	const std::array<double, 2> max = { bb.max().x(), bb.max().y() };
	const std::array<double, 2> size = { bb.max().x() - bb.min().x(), bb.max().y() - bb.min().y() };
	nlohmann::json bbJson;
	bbJson["min"] = min;
	bbJson["max"] = max;
	bbJson["size"] = size;
	return bbJson;
}

template <typename G>
static nlohmann::json getBoundingBox3(G geometry)
{
	const auto& bb = geometry.getBoundingBox();
	const std::array<double, 3> min = { bb.min().x(), bb.min().y(), bb.min().z() };
	const std::array<double, 3> max = { bb.max().x(), bb.max().y(), bb.max().z() };
	const std::array<double, 3> size = { bb.max().x() - bb.min().x(), bb.max().y() - bb.min().y(), bb.max().z() - bb.min().z() };
	nlohmann::json bbJson;
	bbJson["min"] = min;
	bbJson["max"] = max;
	bbJson["size"] = size;
	return bbJson;
}

template <typename C>
static nlohmann::json getCache(C cache)
{
	nlohmann::json cacheJson;
	cacheJson["entries"] = cache->size();
	cacheJson["bytes"] = cache->totalCost();
	cacheJson["max_size"] = cache->maxSizeMB() * 1024 * 1024;
	return cacheJson;
}

}

RenderStatistic::RenderStatistic() : begin(std::chrono::steady_clock::now())
{
}

void RenderStatistic::start()
{
	begin = std::chrono::steady_clock::now();
}

std::chrono::milliseconds RenderStatistic::ms()
{
	const std::chrono::steady_clock::time_point end{std::chrono::steady_clock::now()};
	const std::chrono::milliseconds ms{std::chrono::duration_cast<std::chrono::milliseconds>(end-begin)};
	return ms;
}

void RenderStatistic::printCacheStatistic()
{
	LogVisitor visitor({});
	visitor.printCacheStatistic();
}

void RenderStatistic::printRenderingTime()
{
	LogVisitor visitor({});
	visitor.printRenderingTime(ms());
}

void RenderStatistic::printAll(const shared_ptr<const Geometry> geom, const Camera& camera, const std::vector<std::string>& options, const std::string& filename)
{
	//bool is_log = false;
	std::unique_ptr<StatisticVisitor> visitor;
	if (filename.empty()) {
		//is_log = true;
		visitor = std::make_unique<LogVisitor>(options);
	} else if (filename == "-") {
		visitor = std::make_unique<StreamVisitor>(options, std::cout);
	} else {
		visitor = std::make_unique<StreamVisitor>(options, filename);
	}

	visitor->printCacheStatistic();
	visitor->printRenderingTime(ms());
	if (geom && !geom->isEmpty()) {
		geom->accept(*visitor);
	}
	visitor->printCamera(camera);
	visitor->finish();
}

void LogVisitor::visit(const GeometryList& geomlist)
{
	LOG(message_group::None,Location::NONE,"","Top level object is a list of objects:");
	LOG(message_group::None,Location::NONE,"","   Objects:    %1$d",
	geomlist.getChildren().size());
}

void LogVisitor::visit(const Polygon2d& poly)
{
	LOG(message_group::None,Location::NONE,"","Top level object is a 2D object:");
	LOG(message_group::None,Location::NONE,"","   Contours:   %1$6d", poly.outlines().size());
	if (is_enabled(RenderStatistic::BOUNDING_BOX)) {
		const auto& bb = poly.getBoundingBox();
		LOG(message_group::None,Location::NONE,"","Bounding box:");
		LOG(message_group::None,Location::NONE,"","   Min:  %1$.2f, %2$.2f", bb.min().x(), bb.min().y());
		LOG(message_group::None,Location::NONE,"","   Max:  %1$.2f, %2$.2f", bb.max().x(), bb.max().y());
		LOG(message_group::None,Location::NONE,"","   Size: %1$.2f, %2$.2f", bb.max().x() - bb.min().x(), bb.max().y() - bb.min().y());
	}
	if (is_enabled(RenderStatistic::AREA)) {
		LOG(message_group::None,Location::NONE,"","Measurements:");
		LOG(message_group::None,Location::NONE,"","   Area: %1$.2f", poly.area());
	}
}

void LogVisitor::printBoundingBox3(const BoundingBox& bb)
{
	if (is_enabled(RenderStatistic::BOUNDING_BOX)) {
		LOG(message_group::None,Location::NONE,"","Bounding box:");
		LOG(message_group::None,Location::NONE,"","   Min:  %1$.2f, %2$.2f, %3$.2f", bb.min().x(), bb.min().y(), bb.min().z());
		LOG(message_group::None,Location::NONE,"","   Max:  %1$.2f, %2$.2f, %3$.2f", bb.max().x(), bb.max().y(), bb.max().z());
		LOG(message_group::None,Location::NONE,"","   Size: %1$.2f, %2$.2f, %3$.2f", bb.max().x() - bb.min().x(), bb.max().y() - bb.min().y(), bb.max().z() - bb.min().z());
	}
}

void LogVisitor::visit(const PolySet& ps)
{
	assert(ps.getDimension() == 3);
	LOG(message_group::None,Location::NONE,"","Top level object is a 3D object:");
	LOG(message_group::None,Location::NONE,"","   Facets:     %1$6d", ps.numFacets());
	printBoundingBox3(ps.getBoundingBox());
}

#ifdef ENABLE_CGAL
void LogVisitor::visit(const CGAL_Nef_polyhedron& nef)
{
  if (nef.getDimension() == 3) {
    bool simple = nef.p3->is_simple();
    LOG(message_group::None,Location::NONE,"","Top level object is a 3D object:");
    LOG(message_group::None,Location::NONE,"","   Simple:     %6s",(simple ? "yes" : "no"));
    LOG(message_group::None,Location::NONE,"","   Vertices:   %1$6d",nef.p3->number_of_vertices());
    LOG(message_group::None,Location::NONE,"","   Halfedges:  %1$6d",nef.p3->number_of_halfedges());
    LOG(message_group::None,Location::NONE,"","   Edges:      %1$6d",nef.p3->number_of_edges());
    LOG(message_group::None,Location::NONE,"","   Halffacets: %1$6d",nef.p3->number_of_halffacets());
    LOG(message_group::None,Location::NONE,"","   Facets:     %1$6d",nef.p3->number_of_facets());
    LOG(message_group::None,Location::NONE,"","   Volumes:    %1$6d",nef.p3->number_of_volumes());
    if (!simple) {
      LOG(message_group::UI_Warning,Location::NONE,"","Object may not be a valid 2-manifold and may need repair!");
    }
    printBoundingBox3(nef.getBoundingBox());
  }
}
#endif // ENABLE_CGAL

void LogVisitor::printCamera(const Camera& camera)
{
	if (is_enabled(RenderStatistic::CAMERA)) {
		LOG(message_group::None,Location::NONE,"","Camera:");
		LOG(message_group::None,Location::NONE,"","   Translation: %1$.2f, %2$.2f, %3$.2f", camera.getVpt().x(), camera.getVpt().y(), camera.getVpt().z());
		LOG(message_group::None,Location::NONE,"","   Rotation:    %1$.2f, %2$.2f, %3$.2f", camera.getVpr().x(), camera.getVpr().y(), camera.getVpr().z());
		LOG(message_group::None,Location::NONE,"","   Distance:    %1$.2f", camera.zoomValue());
		LOG(message_group::None,Location::NONE,"","   FOV:         %1$.2f", camera.fovValue());
	}
}

void LogVisitor::printCacheStatistic()
{
	// always enabled
  GeometryCache::instance()->print();
#ifdef ENABLE_CGAL
  CGALCache::instance()->print();
#endif
}

void LogVisitor::printRenderingTime(const std::chrono::milliseconds ms)
{
	// always enabled
  LOG(message_group::None,Location::NONE,"","Total rendering time: %1$d:%2$02d:%3$02d.%4$03d",
    (ms.count() /1000/60/60     ),
    (ms.count() /1000/60 % 60   ),
    (ms.count() /1000    % 60   ),
    (ms.count()          % 1000 ));
}

void LogVisitor::finish()
{
}

void StreamVisitor::visit(const GeometryList& geomlist)
{
}

void StreamVisitor::visit(const Polygon2d& poly)
{
	if (is_enabled(RenderStatistic::GEOMETRY)) {
		nlohmann::json geometryJson;
		geometryJson["dimensions"] = 2;
		geometryJson["convex"] = poly.is_convex();
		geometryJson["contours"] = poly.outlines().size();
		if (is_enabled(RenderStatistic::BOUNDING_BOX)) {
			geometryJson["bounding_box"] = getBoundingBox2(poly);
		}
		json["geometry"] = geometryJson;
	}
}

void StreamVisitor::visit(const PolySet& ps)
{
	if (is_enabled(RenderStatistic::GEOMETRY)) {
		assert(ps.getDimension() == 3);
		nlohmann::json geometryJson;
		geometryJson["dimensions"] = 3;
		geometryJson["convex"] = ps.is_convex();
		geometryJson["facets"] = ps.numFacets();
		if (is_enabled(RenderStatistic::BOUNDING_BOX)) {
			geometryJson["bounding_box"] = getBoundingBox3(ps);
		}
		json["geometry"] = geometryJson;
	}
}

#ifdef ENABLE_CGAL
void StreamVisitor::visit(const CGAL_Nef_polyhedron& nef)
{
	if (is_enabled(RenderStatistic::GEOMETRY)) {
		nlohmann::json geometryJson;
		geometryJson["dimensions"] = 3;
		geometryJson["simple"] = nef.p3->is_simple();
		geometryJson["vertices"] = nef.p3->number_of_vertices();
		geometryJson["edges"] = nef.p3->number_of_edges();
		geometryJson["facets"] = nef.p3->number_of_facets();
		geometryJson["volumes"] = nef.p3->number_of_volumes();
		if (is_enabled(RenderStatistic::BOUNDING_BOX)) {
			geometryJson["bounding_box"] = getBoundingBox3(nef);
		}
		json["geometry"] = geometryJson;
	}
}
#endif // ENABLE_CGAL

void StreamVisitor::printCamera(const Camera& camera)
{
	if (is_enabled(RenderStatistic::CAMERA)) {
		const std::array<double, 3> translation = { camera.getVpt().x(), camera.getVpt().y(), camera.getVpt().z() };
		const std::array<double, 3> rotation = { camera.getVpr().x(), camera.getVpr().y(), camera.getVpr().z() };
		nlohmann::json cameraJson;
		cameraJson["translation"] = translation;
		cameraJson["rotation"] = rotation;
		cameraJson["distance"] = camera.zoomValue();
		cameraJson["fov"] = camera.fovValue();
		json["camera"] = cameraJson;
	}
}

void StreamVisitor::printCacheStatistic()
{
	if (is_enabled(RenderStatistic::CACHE)) {
		nlohmann::json cacheJson;
		cacheJson["geometry_cache"] = getCache(GeometryCache::instance());
#ifdef ENABLE_CGAL
		cacheJson["cgal_cache"] = getCache(CGALCache::instance());
#endif // ENABLE_CGAL
		json["cache"] = cacheJson;
	}
}

void StreamVisitor::printRenderingTime(const std::chrono::milliseconds ms)
{
	if (is_enabled(RenderStatistic::TIME)) {
		nlohmann::json timeJson;
		timeJson["time"] = (boost::format("%1$d:%2$02d:%3$02d.%4$03d")
						% (ms.count() / 1000 / 60 / 60)
						% (ms.count() / 1000 / 60 % 60)
						% (ms.count() / 1000 % 60)
						% (ms.count() % 1000)).str();
		timeJson["total"] = ms.count();
		timeJson["milliseconds"] = ms.count() % 1000;
		timeJson["seconds"] = ms.count() / 1000 % 60;
		timeJson["minutes"] = ms.count() / 1000 / 60 % 60;
		timeJson["hours"] = ms.count() / 1000 / 60 / 60;
		json["time"] = timeJson;
	}
}

void StreamVisitor::finish()
{
	stream << json;
}
