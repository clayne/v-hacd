/* Copyright (c) 2011 Khaled Mamou (kmamou at gmail dot com)
 All rights reserved.
 
 
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 
 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 
 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 
 3. The names of the contributors may not be used to endorse or promote products derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define _CRT_SECURE_NO_WARNINGS
#include <sstream>
#include <VHACDGraph.h>
#include <VHACDHACD.h>
#include <string.h>
#include <algorithm>
#include <iterator>
#include <limits>
#include <VHACDMeshDecimator.h>
#include <btConvexHullComputer.h>
#include <limits>
#include <fstream>

//#define VHACD_DEBUG
namespace VHACD
{ 

	void VHACD::AddEdge(long v1, long v2)
    {
		if (m_graph.GetEdgeID(v1, v2) == -1)
		{
			m_graph.AddEdge(v1, v2);
		}
	}

	void VHACD::CreateGraph()
    {
		std::vector< Sphere > spheres;
		const size_t nCH = m_convexHulls->size();
		spheres.resize(nCH);		
		for(size_t p = 0; p < nCH; ++p)
		{
			(*m_convexHulls)[p]->ComputeBB();
			spheres[p].m_center = 0.5 * ((*m_convexHulls)[p]->GetMinBB() + (*m_convexHulls)[p]->GetMaxBB());
			spheres[p].m_radius = 0.5 * ((*m_convexHulls)[p]->GetMaxBB() - (*m_convexHulls)[p]->GetMinBB()).GetNorm();
		}
		m_graph.Clear();
		m_graph.Allocate(nCH, 5 * nCH);

		for(size_t p1 = 0; p1 < nCH; ++p1)
		{
			m_graph.m_vertices[p1].m_volume     = fabs((*m_convexHulls)[p1]->ComputeVolume());
			
			for(size_t p2 = p1+1; p2 < nCH; ++p2)
			{
				if ( (spheres[p2].m_center-spheres[p1].m_center).GetNorm() < spheres[p1].m_radius+spheres[p2].m_radius )
				{
					m_graph.AddEdge(p1, p2);
				}
			}
		}

    }

	VHACD::VHACD()
	{
        m_nClusters = 0;
        m_concavity = 0.0;
        m_diag = 1.0;
        m_nVerticesPerCH = 30;
		m_callBack = 0;
		m_nMinClusters = 3;
		m_convexHulls = 0;
//		m_finalConvexHulls = 0;
	}																
	VHACD::~VHACD(void)
	{
	}

    void VHACD::ComputeEdgeCost(size_t e)
    {
		GraphEdge & gE = m_graph.m_edges[e];
        long v1 = gE.m_v1;
        long v2 = gE.m_v2;

		const GraphVertex & gV1 = m_graph.m_vertices[v1];
		const GraphVertex & gV2 = m_graph.m_vertices[v2];

		Mesh * m1 = (*m_convexHulls)[v1];
		Mesh * m2 = (*m_convexHulls)[v2];

		const size_t n1 = m1->GetNPoints();
		const size_t n2 = m2->GetNPoints();
		const size_t n = n1 + n2;

		Vec3< Real > * points = new Vec3< Real > [n1+n2];
				
		for(size_t v = 0; v < n1; ++v)
		{
			points[v] = m1->GetPoint(v);
		}
		for(size_t v = n1; v < n; ++v)
		{
			points[v] = m2->GetPoint(v-n1);
		}
		
		btConvexHullComputer ch;
		ch.compute((Real *)points, 3 * sizeof(Real), n , -1.0, -1.0); 
		delete [] points;


		delete gE.m_convexHull;

		gE.m_convexHull = new Mesh;

		for(int v = 0; v < ch.vertices.size(); v++)
		{			
			gE.m_convexHull->AddPoint(Vec3<Real>(ch.vertices[v].getX(), ch.vertices[v].getY(), ch.vertices[v].getZ()));
		}
		const int nt = ch.faces.size();
		for(int t = 0; t < nt; ++t)
		{

			const btConvexHullComputer::Edge * sourceEdge = & (ch.edges[ch.faces[t]]);
			int a = sourceEdge->getSourceVertex();
			int b = sourceEdge->getTargetVertex();
			const btConvexHullComputer::Edge * edge = sourceEdge->getNextEdgeOfFace();
			int c = edge->getTargetVertex();
			while (c != a)
			{				
				gE.m_convexHull->AddTriangle(Vec3<long>(a, b, c));				
				edge = edge->getNextEdgeOfFace();
				b = c;
				c = edge->getTargetVertex();
			}
		}
		gE.m_volume = gE.m_convexHull->ComputeVolume();
		gE.m_concavity = gE.m_volume - gV1.m_volume - gV2.m_volume; //fabs(gE.m_volume - gV1.m_volume - gV2.m_volume);		
	}

    bool VHACD::InitializePriorityQueue()
    {
		m_pqueue.reserve(m_graph.m_nE + 100);
        for (size_t e=0; e < m_graph.m_nE; ++e) 
        {
			ComputeEdgeCost(static_cast<long>(e));
			m_pqueue.push(GraphEdgePriorityQueue(static_cast<long>(e), m_graph.m_edges[e].m_concavity, std::min(m_graph.m_edges[e].m_v1, m_graph.m_edges[e].m_v2)));
        }
		return true;
    }
	void VHACD::Simplify()
	{

		long v1 = -1;
        long v2 = -1;        
        double progressOld = -1.0;
        double progress = 0.0;
        double globalConcavity  = 0.0;     
		char msg[1024];
		double ptgStep = 1.0;
		const size_t V0 = m_graph.GetNVertices();
		std::vector<GraphEdgePriorityQueue> highPriority;
		while ( (!m_pqueue.empty()) || (highPriority.size() > 0)) 
		{
            progress = 100.0-m_graph.GetNVertices() * 100.0 / V0;
            if (fabs(progress-progressOld) > ptgStep && m_callBack)
            {
				sprintf(msg, "%3.2f %% V = %lu \t C = %f \t \t \r", progress, static_cast<unsigned long>(m_graph.GetNVertices()), globalConcavity);
				(*m_callBack)(msg);
                progressOld = progress;
				if (progress > 99.0)
				{
					ptgStep = 0.01;
				}
				else if (progress > 90.0)
				{
					ptgStep = 0.1;
				}
            }

			GraphEdgePriorityQueue currentEdge(0,0.0, -1);
			bool done = false;

			do
			{
				done = false;
				if (highPriority.size() == 0)
				{
					done = true;
                    break;
				}
				currentEdge = highPriority[highPriority.size()-1];
				highPriority.pop_back();
                m_pqueue.pop();
			}
			while (  m_graph.m_edges[currentEdge.m_name].m_deleted || 
					 m_graph.m_edges[currentEdge.m_name].m_concavity != currentEdge.m_priority);

			if (done)
			{
				do
				{
					done = false;
					if (m_pqueue.size() == 0)
					{
						done = true;
						break;
					}
					currentEdge = m_pqueue.top();
					m_pqueue.pop();
				}
				while (  m_graph.m_edges[currentEdge.m_name].m_deleted || 
						 m_graph.m_edges[currentEdge.m_name].m_concavity != currentEdge.m_priority);
			}
			if (!done)
			{
				if ((m_graph.m_edges[currentEdge.m_name].m_concavity < m_concavity) && 
						(globalConcavity < m_concavity)                                && 
						(m_graph.GetNVertices() > m_nMinClusters)                      && 
						(m_graph.GetNEdges() > 0))
				{
					v1 = m_graph.m_edges[currentEdge.m_name].m_v1;
					v2 = m_graph.m_edges[currentEdge.m_name].m_v2;	
					globalConcavity = std::max<double>(globalConcavity ,m_graph.m_edges[currentEdge.m_name].m_concavity);
					GraphEdge & gE = m_graph.m_edges[currentEdge.m_name];
					GraphVertex & gV1 = m_graph.m_vertices[v1];
					GraphVertex & gV2 = m_graph.m_vertices[v2];
					// update vertex info
					gV1.m_concavity     = gE.m_concavity;
					delete (*m_convexHulls)[v1];
					delete (*m_convexHulls)[v2];				
					(*m_convexHulls)[v1] = gE.m_convexHull;
					(*m_convexHulls)[v2] = 0;
					gE.m_convexHull = 0;
					gV1.m_volume = gE.m_volume;
					gV1.m_concavity = gE.m_concavity;
					m_graph.EdgeCollapse(v1, v2);
					long idEdge;
					for(size_t itE = 0; itE < m_graph.m_vertices[v1].m_edges.Size(); ++itE)
					{
						idEdge = m_graph.m_vertices[v1].m_edges[itE];
						ComputeEdgeCost(idEdge);
						m_pqueue.push(GraphEdgePriorityQueue(idEdge, m_graph.m_edges[idEdge].m_concavity, std::min(m_graph.m_edges[idEdge].m_v1, m_graph.m_edges[idEdge].m_v2)));
/*
						if (m_graph.m_edges[idEdge].m_concavity > gV1.m_concavity)
						{
							m_pqueue.push(GraphEdgePriorityQueue(idEdge, m_graph.m_edges[idEdge].m_concavity));
						}
						else
						{
							highPriority.push_back(GraphEdgePriorityQueue(idEdge, m_graph.m_edges[idEdge].m_concavity));
						}
*/
					}
				}
				else
				{
					break;
				}
			}
		}
		m_nClusters = m_graph.GetNVertices();
		m_finalConvexHullsIndices.resize(m_nClusters);
		for (size_t p=0, v = 0; v != m_graph.m_vertices.size(); ++v) 
		{
			if (!m_graph.m_vertices[v].m_deleted)
			{
				m_finalConvexHullsIndices[p] = v;
                if (m_callBack) 
                {
                    char msg[1024];
					sprintf(msg, "\t CH(%lu) \t %f \t %lu\n", v, static_cast<float>(m_graph.m_vertices[v].m_concavity), (*m_convexHulls)[v]->GetNPoints());
					(*m_callBack)(msg);
					p++;
                }
			}
		}
        if (m_callBack)
        {
			sprintf(msg, "# clusters =  %lu \t C = %f\n", static_cast<unsigned long>(m_nClusters), globalConcavity);
			(*m_callBack)(msg);
        }
	}

    bool VHACD::Init()
    {
		CreateGraph();
		return true;
	}


    bool VHACD::Compute()
    {
		if (!m_convexHulls) return false;
		if (m_callBack)
		{
			std::ostringstream msg;
			msg << "+ Mesh" << std::endl;
			msg << "\t # initial convex-hulls         \t" << m_convexHulls->size() << std::endl;
			msg << "+ Parameters" << std::endl;
			msg << "\t min # of clusters              \t" << m_nMinClusters << std::endl;
			msg << "\t max concavity                  \t" << m_concavity << std::endl;
			msg << "\t # vertices per convex-hull     \t" << m_nVerticesPerCH << std::endl;
//			msg << "\t produce full convex-hulls      \t" << fullCH << std::endl;	
			(*m_callBack)(msg.str().c_str());
		}		
        // Compute volumes of convex-hulls
		if (m_callBack) (*m_callBack)("+ Initializing Priority Queue\n");
        InitializePriorityQueue();
        // we simplify the graph		
		if (m_callBack) (*m_callBack)("+ Simplification ...\n");
		Simplify();
        return true;
    }

	bool SaveConvexHulls(const std::string & fileName, const std::vector< Mesh * > & convexHulls)
	{
		std::ofstream foutCH(fileName.c_str());
		if (foutCH.is_open())
		{
			for(size_t p = 0; p < convexHulls.size(); ++p)
			{
				Material mat;
				mat.m_diffuseColor.X() = mat.m_diffuseColor.Y() = mat.m_diffuseColor.Z() = 0.0;
				while (mat.m_diffuseColor.X() == mat.m_diffuseColor.Y() ||
					   mat.m_diffuseColor.Z() == mat.m_diffuseColor.Y() ||
					   mat.m_diffuseColor.Z() == mat.m_diffuseColor.X()  )
				{
					mat.m_diffuseColor.X() = (rand()%100) / 100.0;
					mat.m_diffuseColor.Y() = (rand()%100) / 100.0;
					mat.m_diffuseColor.Z() = (rand()%100) / 100.0;
				}
				convexHulls[p]->SaveVRML2(foutCH, mat);
			}
			foutCH.close();
			return true;
		}
		else
		{
			return false;
		}	
	}

	bool ComputeClipPlanes(Real minD, Real maxD, int posSampling, int angleSampling, std::set< Plane > & planes)
	{
		Real a, b, c, d;
		const Real inc = PI * (3.0 - sqrt(5.0));
		const Real off = 1.0 / angleSampling;
		Real phi, y, r;
		Real deltaPos = 1.0 / posSampling;
		Real t = 0.0;
		for(int i=0; i <= posSampling; ++i)
		{	
			d = (1.0 - t) * minD + t * maxD;
			t += deltaPos;

			planes.insert(Plane(1.0, 0.0, 0.0, d));
			planes.insert(Plane(0.0, 1.0, 0.0, d));
			planes.insert(Plane(0.0, 0.0, 1.0, d));
			
			for(int j=0; j < angleSampling; ++j)
			{
				y = j * off - 1.0 + (off / 2.0);
				r = sqrt(1.0 - y*y);
				phi = j * inc;
				a = cos(phi)*r;
				b = y;
				c = sin(phi)*r;
				planes.insert(Plane(a, b, c, d));																					
			}			
		}
		return true;
	}

	bool RefineClipPlanes(Plane p0, Real minD, Real maxD, int posSampling, int angleSampling, std::set< Plane > & planes)
	{
		Real a, b, c, d;
		const Real inc = PI * (3.0 - sqrt(5.0));
		const Real off = 1.0 / angleSampling;
		Real phi, y, r;
		Real deltaPos = (maxD-minD)  / posSampling;
		for(int i=-posSampling; i <= posSampling; ++i)
		{	
			d = p0.m_d + i * deltaPos;
			planes.insert(Plane(1.0, 0.0, 0.0, d));
			planes.insert(Plane(0.0, 1.0, 0.0, d));
			planes.insert(Plane(0.0, 0.0, 1.0, d));

			for(int j=0; j < angleSampling; ++j)
			{
				y = j * off - 1.0 + (off / 2.0);
				r = sqrt(1.0 - y*y);
				phi = j * inc;
				a = cos(phi)*r;
				b = y;
				c = sin(phi)*r;
				planes.insert(Plane(a, b, c, d));
			}			
		}
		return true;
	}

	void ComputeBestClippingPlane(const Mesh & inputMesh, 
        Real volume0, 
        Real volume, 
        std::set< Plane > & planes, 
		Plane & bestPlane, 
		Mesh & bestLeft, 
		Mesh & bestRight,
		Real & minConcavity,
		Real & minBalance,
		Real alpha,
		CallBackFunction callBack, bool debug)
	{
		std::set< Plane >::const_iterator itEnd = planes.end();
		std::set< Plane >::const_iterator itBegin = planes.begin();
		int i = 0;
		int iBest;
		char fileName[1024];
		Plane plane;
		Real balance, concavity;

		printf("Nunber of clipping planes %i\n", planes.size());
		size_t nV0 = inputMesh.GetNPoints();
		long * v2CCLeft = new long[nV0];
		long * v2CCRight = new long[nV0];
		long * mapping = new long[nV0];
		size_t nV;


		for (std::set< Plane >::const_iterator it = itBegin; it != itEnd; ++it)
		{
			plane  = (*it);
			Mesh left;
			Mesh right;				
			inputMesh.Clip(plane.m_a, plane.m_b, plane.m_c, plane.m_d, &right, &left, true);	

			nV = right.GetNPoints() + left.GetNPoints();
			if ( nV > nV0)
			{
				nV0 = nV;
				delete [] v2CCLeft;
				delete [] v2CCRight;
				delete [] mapping;
				v2CCLeft  = new long[nV0];
				v2CCRight = new long[nV0];
				mapping   = new long[nV0];
			}

			right.CleanDuplicatedVectices();
			left.CleanDuplicatedVectices();

			Mesh leftCH;
			Mesh rightCH;

			// Extract CCs of left
			size_t nCCRight = right.ComputeConnectedComponents(v2CCRight);
			size_t nCCLeft  = left.ComputeConnectedComponents(v2CCLeft);
			size_t nCC      = nCCRight + nCCLeft;
			
			if ( nCCLeft > 1 || nCCRight > 1)
			{
				std::vector< Mesh * > parts;
				std::vector< Mesh > CCs;
				parts.resize(nCC);
				CCs.resize(nCC);

				for(size_t n = 0; n < nCC; n++)
				{
					parts[n] = new Mesh;
				}

				for(size_t n = 0; n < nCCLeft; n++)
				{
					left.ExtractConnectedComponent(n, v2CCLeft, mapping, CCs[n+nCCRight]);
					CCs[n+nCCRight].ComputeConvexHull(*(parts[n+nCCRight]));
					if (debug)
					{
//						sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\left_%06i_CC_%i.wrl", i, n);
//						parts[n+nCCRight]->SaveVRML2(fileName);
					}
				}

				for(size_t n = 0; n < nCCRight; n++)
				{
					right.ExtractConnectedComponent(n, v2CCRight, mapping, CCs[n]);
					CCs[n].ComputeConvexHull(*(parts[n]));
					if (debug)
					{
//						sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\right_%06i_CC_%i.wrl", i, n);
//						parts[n]->SaveVRML2(fileName);
					}
				}

				VHACD vhacd;

				vhacd.SetNClusters(2);										// minimum number of clusters
				vhacd.SetConcavity(std::numeric_limits<double>::max());     // maximum concavity
				vhacd.SetCallBack(0 /*callBack*/);
				vhacd.SetInitialConvexHulls(& parts);

				vhacd.Init();

				for(size_t n1 = 0; n1 < nCCRight; n1++)
				{
					for(size_t n2 = n1+1; n2 < nCCRight; n2++)
					{
						vhacd.AddEdge(n1, n2);
					}
				}
				for(size_t n1 = nCCRight; n1 < nCC; n1++)
				{
					for(size_t n2 = n1+1; n2 < nCC; n2++)
					{
						vhacd.AddEdge(n1, n2);
					}
				}
				vhacd.Compute();

				const std::vector< long > &	 res = vhacd.GetFinalConvexHullsIndices();
				const SArray<long, SARRAY_DEFAULT_MIN_SIZE> &  ancestors0 = vhacd.GetVertexAncestors(res[0]);
				const SArray<long, SARRAY_DEFAULT_MIN_SIZE> &  ancestors1 = vhacd.GetVertexAncestors(res[1]);
				
				left  = CCs[res[0]];
				right = CCs[res[1]];
				Vec3< long > tri;
				for(size_t n = 0; n < ancestors0.Size(); ++n)
				{
					size_t shiftPts = left.GetNPoints();
					size_t shiftTris = left.GetNTriangles();
					const Mesh & mesh = CCs[ancestors0[n]];
					size_t nPts  = mesh.GetNPoints();
					size_t nTris = mesh.GetNTriangles();
					left.ResizePoints(shiftPts   + nPts);
					left.ResizeTriangles(shiftTris + nTris);
					for(size_t v = 0; v < nPts; ++v)
					{
						left.SetPoint(v + shiftPts, mesh.GetPoint(v)); 
					}
					for(size_t t = 0; t < nTris; ++t)
					{
						tri = mesh.GetTriangle(t);
						tri += shiftPts;
						left.SetTriangle(t + shiftTris, tri); 
					}
				}
				for(size_t n = 0; n < ancestors1.Size(); ++n)
				{
					size_t shiftPts = right.GetNPoints();
					size_t shiftTris = right.GetNTriangles();
					const Mesh & mesh = CCs[ancestors1[n]];
					size_t nPts  = mesh.GetNPoints();
					size_t nTris = mesh.GetNTriangles();
					right.ResizePoints(shiftPts   + nPts);
					right.ResizeTriangles(shiftTris + nTris);
					for(size_t v = 0; v < nPts; ++v)
					{
						right.SetPoint(v + shiftPts, mesh.GetPoint(v)); 
					}
					for(size_t t = 0; t < nTris; ++t)
					{
						tri = mesh.GetTriangle(t);
						tri += shiftPts;
						right.SetTriangle(t + shiftTris, tri); 
					}
				}
				for(size_t n = 0; n < nCC; n++)
				{
					delete parts[n];
				}
				if (ancestors1.Size() > 0)
				{
					right.CleanDuplicatedVectices();
				}
				if (ancestors0.Size() > 0)
				{
					left.CleanDuplicatedVectices();
				}
			}

			if (left.GetNPoints() > 0)
			{
				left.ComputeConvexHull(leftCH);
			}
			if (right.GetNPoints() > 0)
			{
				right.ComputeConvexHull(rightCH);
			}

			Real volumeLeft    = fabs(left.ComputeVolume());
			Real volumeRight   = fabs(right.ComputeVolume());
			
			Real volumeLeftCH  = 0.0;
			Real volumeRightCH = 0.0;

			if (leftCH.GetNPoints() > 0)
			{
				volumeLeftCH  = leftCH.ComputeVolume();
			}
			if (rightCH.GetNPoints() > 0)
			{
				volumeRightCH = rightCH.ComputeVolume();
			}
				
			concavity = (volumeLeftCH + volumeRightCH - volume) / volume0;
			balance = fabs(volumeLeft-volumeRight) / volume0;
			if (debug)
			{
//				printf("-> V=%f \t VL=%f \t VR=%f (%f) \n \t VL_CH=%f \t VR_CH=%f C = %f\n", volume, volumeLeft, volumeRight, volume - volumeLeft - volumeRight, volumeLeftCH, volumeRightCH, concavity);

				printf("-> C=%f \t B=%f \t E=%f\n", concavity, balance, volume - volumeLeft - volumeRight);

				sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\left_%06i.wrl", i);
				left.SaveVRML2(fileName);

				sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\right_%06i.wrl", i);
				right.SaveVRML2(fileName);		

				sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\leftCH_%06i.wrl", i);
				leftCH.SaveVRML2(fileName);

				sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\rightCH_%06i.wrl", i);
				rightCH.SaveVRML2(fileName);		
			}
			if (concavity + alpha * balance <  minConcavity + alpha * minBalance)
			{
				bestPlane    = plane;
				minConcavity = concavity;
				iBest	     = i;
				bestLeft	 = left;
				bestRight	 = right;
				minBalance   = balance;
			}
			i++;
		}
		delete [] v2CCRight;
		delete [] v2CCLeft;
		delete [] mapping;
		printf("Best %i \t C=%f \t B=%f \t (%f, %f, %f, %f)\n", iBest,  minConcavity, minBalance, bestPlane.m_a, bestPlane.m_b, bestPlane.m_c, bestPlane.m_d);
	}

	bool ApproximateConvexDecomposition(const Mesh & inputMesh, int depth, 
										int posSampling, int angleSampling,
										int posRefine, int angleRefine,
										Real alpha, Real concavityThreshold,
                                        std::vector< Mesh * > & parts, CallBackFunction callBack)
	{
		if (callBack)
		{
			std::ostringstream msg;
			msg << "+ Mesh" << std::endl;
			msg << "\t # vertices                     \t" << inputMesh.GetNPoints() << std::endl;
			msg << "\t # triangles                    \t" << inputMesh.GetNTriangles() << std::endl;
			msg << "+ Parameters" << std::endl;
			msg << "\t depth			              \t" << depth << std::endl;
			msg << "\t concavityThreshold			  \t" << concavityThreshold << std::endl;
			msg << "\t alpha			              \t" << alpha << std::endl;
			msg << "\t # of position samples          \t" << posSampling << std::endl;
			msg << "\t # of angle samples	          \t" << angleSampling << std::endl;
			msg << "\t refine position parameter      \t" << posRefine << std::endl;
			msg << "\t refine angle parameter         \t" << angleRefine << std::endl;

			(*callBack)(msg.str().c_str());
			(*callBack)("+ Subdividing the mesh \n");
		}
#ifdef VHACD_DEBUG		
        char fileName[1024];
#endif

        std::vector< Mesh * > inputParts;
        inputParts.reserve(static_cast<size_t>(pow(2.0, depth)));
        inputParts.push_back(new Mesh);
        *(inputParts[0]) = inputMesh;

        
        Real volume0 = fabs(inputParts[0]->ComputeVolume());

        int sub = 0;
        while( sub++ < depth && inputParts.size() > 0)
        {
            const size_t nInputParts = inputParts.size();
            std::vector< Mesh * > temp;
            temp.reserve(nInputParts);
            for(size_t p = 0; p < nInputParts; ++p)
            {
                Mesh * mesh = inputParts[p];
                mesh->ComputeBB();
		        Real volume = fabs(mesh->ComputeVolume());

                Mesh ch;
                mesh->ComputeConvexHull(ch);
                Real volumeCH = fabs(ch.ComputeVolume());

                Real concavity = (volumeCH - volume) / volume0;

                if (concavity > concavityThreshold)
                {
		            Vec3< Real > p0 = mesh->GetMinBB();
		            Vec3< Real > p1 = mesh->GetMaxBB();
		            Real minD = std::min(std::min(p0.X(), p0.Y()), p0.Z());
		            Real maxD = std::max(std::max(p1.X(), p1.Y()), p1.Z());

		            std::set< Plane > planes;
		            ComputeClipPlanes(minD, maxD, posSampling, angleSampling, planes);
		            Plane bestPlane;
		            Mesh  * bestLeft = new Mesh;
		            Mesh  * bestRight = new Mesh;
                    temp.push_back(bestLeft);
                    temp.push_back(bestRight);

		            Real minConcavity = std::numeric_limits<double>::max();
		            Real minBalance = std::numeric_limits<double>::max();

		            ComputeBestClippingPlane(*mesh, volume0, volume, planes, bestPlane, *bestLeft, *bestRight, minConcavity,  minBalance, alpha, callBack, false);
#ifdef VHACD_DEBUG
		            sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\best_left_sub%i_part%i.wrl", sub, p);
		            bestLeft->SaveVRML2(fileName);

		            sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\best_right_sub%i_part%i.wrl", sub, p);
		            bestRight->SaveVRML2(fileName);		
#endif 
		            Real delta   = (maxD - minD) / (posSampling);
		            Real minDRef = bestPlane.m_d - delta;
		            Real maxDRef = bestPlane.m_d + delta;
		            std::set< Plane > planesRef;

		            RefineClipPlanes(bestPlane, minDRef, maxDRef, posRefine, angleRefine * angleSampling, planesRef);
		            ComputeBestClippingPlane(*mesh, volume0, volume, planesRef, bestPlane, *bestLeft, *bestRight, minConcavity,  minBalance, alpha, callBack, false);
#ifdef VHACD_DEBUG
		            sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\best_sub%i_part%i_ref.wrl", sub, 2*p);
		            bestLeft->SaveVRML2(fileName);

		            sprintf(fileName, "C:\\git\\v-hacd\\data\\test\\best_sub%i_part%i_ref.wrl", sub, 2*p+1);
		            bestRight->SaveVRML2(fileName);		
#endif 

                    delete mesh;
                }
                else
                {
                    parts.push_back(mesh);
                }
            }
            inputParts = temp;
        }
        const size_t nInputParts = inputParts.size();
        for(size_t p = 0; p < nInputParts; ++p)
        {
            parts.push_back(inputParts[p]);
        }
		return true;
	}
}