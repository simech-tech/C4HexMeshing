#include "MC3D/Algorithm/MotorcycleTracer.hpp"

namespace mc3d
{

MotorcycleTracer::MotorcycleTracer(TetMeshProps& meshProps, MotorcycleQueue& mQ, bool simulateBC)
    : TetMeshNavigator(meshProps), TetMeshManipulator(meshProps), _mQ(mQ), _qPops(0), _eSplits(0),
      _simulateBC(simulateBC)
{
}

MotorcycleTracer::RetCode MotorcycleTracer::traceAllMotorcycles()
{
    double maxDist = 0.0;
    while (!_mQ.empty())
    {
        maxDist = std::max(maxDist, _mQ.top().dist.get_d());
        DLOG_IF(INFO, _qPops % 10000 == 0)
            << "After " << _qPops << " queue pops: " << _eSplits << " edges split, queue size is " << _mQ.size();
        auto ret = traceNextMotorcycle();
        if (ret != SUCCESS)
            return ret;
    }
    DLOG(INFO) << "Finished queue processing after " << _qPops << " queue pops and " << _eSplits << " edge splits";
    DLOG(INFO) << "Furthest parametric distance travelled by a motorcycle: " << maxDist;
    return SUCCESS;
}

MotorcycleTracer::RetCode MotorcycleTracer::traceNextMotorcycle()
{
    _localQ.push(_mQ.top());
    _mQ.pop();
    _qPops++;

    while (!_localQ.empty())
    {
        Motorcycle mot = _localQ.top();
        _localQ.pop();
        if (!meshProps().mesh().is_deleted(mot.tet))
        {
            // tet and edge havent been split
            auto ret = traceMotorcycle(mot);
            if (ret != SUCCESS)
                return ret;
        }
        else
        {
            // Tet (and possibly edge) split -> find and trace all children
            RetCode ret;
            forEachChildMotorcycle(mot,
                                   [this, &ret](const Motorcycle& child)
                                   {
                                       if ((ret = traceMotorcycle(child)) != SUCCESS)
                                           return;
                                   });
            if (ret != SUCCESS)
                return ret;
        }
    }
    return SUCCESS;
}

void MotorcycleTracer::forEachChildMotorcycle(const Motorcycle& mot,
                                              std::function<void(const Motorcycle& M)>&& func) const
{
    // tet and (possibly) edge have been split
    // Find all valid combinations of child tets and edges
    list<EH> validChildrenEdges;
    validChildrenEdges.push_back(mot.edge);
    list<CH> validChildrenTets;
    validChildrenTets.push_back(mot.tet);

    Motorcycle motChild = mot;
    for (auto itTet = validChildrenTets.begin(); itTet != validChildrenTets.end();)
    {
        if (meshProps().mesh().is_deleted(*itTet))
        {
            const auto& tetsChildren = meshProps().get<CHILD_CELLS>(*itTet);
            assert(!tetsChildren.empty());
            for (CH child : tetsChildren)
                validChildrenTets.push_back(child);
        }
        else
        {
            // Tet is valid
            auto teItPair = meshProps().mesh().cell_edges(*itTet);
            vector<EH> tetEdges(teItPair.first, teItPair.second);
            for (auto itEdge = validChildrenEdges.begin(); itEdge != validChildrenEdges.end();)
            {
                if (meshProps().mesh().is_deleted(*itEdge))
                {
                    const auto& esChildren = meshProps().get<CHILD_EDGES>(*itEdge);
                    assert(!esChildren.empty());
                    for (EH child : esChildren)
                        validChildrenEdges.push_back(child);
                    itEdge = validChildrenEdges.erase(itEdge);
                }
                else
                {
                    // Edge is valid
                    if (std::find(tetEdges.begin(), tetEdges.end(), *itEdge) != tetEdges.end())
                    {
                        motChild.tet = *itTet;
                        motChild.edge = *itEdge;

                        if (TetMeshNavigator::orientationRelativeToTet(motChild) != Orientation::OUTSIDE)
                        {
                            // valid child combination
                            func(motChild);
                            itEdge = validChildrenEdges.erase(itEdge);
                            break; // Only one child edge can match each child tet
                        }
                    }
                    itEdge++;
                }
            }
        }
        itTet++;
    }
}

MotorcycleTracer::RetCode MotorcycleTracer::traceMotorcycle(const Motorcycle& mot)
{
    //                       D __
    //                       |\  \___
    //                       |       \__
    //                       |         (N)_ <------------ This edge AD may be cut by
    //                       |             \__            - creating new vertex N between A and D
    //                       |     \          \__         - creating new edge BN
    //                       |                   \_       - creating new edge CN
    //                       |                 ___/ A     - creating new face BCN between BCD and ABC
    //                       |            ____/     |     - splitting face ABD -> ABN + BDN
    //                       |       ____/          |     - splitting face ACD -> ACN + CDN
    //                       |  ____/    \          |     - splitting tet ABCD -> ABCN + BCDN
    //                       B_/                    |     - equivalent operations in all other tets adjacent to edge AD
    //                        \__                   |
    //                           \__                |
    //                              \__       \     |
    //                                 \__          |
    // BC is mot.edge ------------------> \__       |
    // (arbitrarily chosen)                  \__    |
    //                                          \_\ |
    //                                             C
    //
    //  mot.edge is BC. New wall face is either BCD, BCA or BCN where N is the vtx created by splitting (half)edge DA.
    const TetMesh& tetMesh = meshProps().mesh();

    // Gather the relevant mesh elements
    TetElements elems(TetMeshNavigator::getTetElements(mot.tet, mot.edge));

    // Determine if propagation direction passes through mot.tet
    Vec3Q uvwA = meshProps().get<CHART>(mot.tet).at(elems.vA);
    Vec3Q uvwD = meshProps().get<CHART>(mot.tet).at(elems.vD);

    int wallIsoCoord = mot.isoCoord();
    Q deltaA = uvwA[wallIsoCoord] - Q(mot.isoValue);
    Q deltaD = uvwD[wallIsoCoord] - Q(mot.isoValue);

    assert(deltaA * deltaD <= 0); // Passes through its tet

    // Determine if wall propagates through isofacet or by splitting mot.tet
    bool isoFacetCBA = deltaA == 0;
    bool isoFacetBCD = deltaD == 0;

    // The face that is to be marked as a wall
    HFH newWallHf;
    // The edges to propagate across
    HEH heNext1;
    HEH heNext2;

    if ((!isoFacetCBA && !isoFacetBCD))
    {
        // Splitting is necessary
        Q t = deltaA / (uvwA[wallIsoCoord] - uvwD[wallIsoCoord]);
        VH vN = TetMeshManipulator::splitHalfEdge(elems.heAD, mot.tet, t);
        _eSplits++;

        // Mark the newly created splitting face as a new MC wall
        newWallHf = tetMesh.find_halfface({elems.vB, elems.vC, vN});
        // Propagate to the two (newly created) edges of the new wall face
        heNext1 = tetMesh.find_halfedge(vN, elems.vB);
        heNext2 = tetMesh.find_halfedge(elems.vC, vN);
    }
    else
    {
        // No splitting needed, propagate through isofacet
        if (isoFacetBCD && isoFacetCBA)
            return DEGENERATE_CHART;
        // Mark the isoFacet as a new MC wall
        newWallHf = isoFacetBCD ? elems.hfBCD : elems.hfCBA;
        HEH heCurrent = isoFacetBCD ? elems.heBC : tetMesh.opposite_halfedge_handle(elems.heBC);

        // Propagate to the other two edges of the wall face
        heNext1 = tetMesh.next_halfedge_in_halfface(heCurrent, newWallHf);
        heNext2 = tetMesh.prev_halfedge_in_halfface(heCurrent, newWallHf);
    }

    FH newWall = tetMesh.face_handle(newWallHf);
    // Skip if already a wall or a boundary
    if (meshProps().get<IS_WALL>(newWall) || tetMesh.is_boundary(newWall))
        return SUCCESS;
    meshProps().set<IS_WALL>(newWall, true);
    meshProps().set<WALL_DIST>(newWall, (float)mot.dist.get_d());
    newWalls.emplace_back(newWall);

    // only push the followup motorcycle into the queue if the edges are still alive
    if (isAlive(tetMesh.edge_handle(heNext1)))
        propagateAcrossEdge(mot, heNext1, newWallHf);
    if (isAlive(tetMesh.edge_handle(heNext2)))
        propagateAcrossEdge(mot, heNext2, newWallHf);

    return SUCCESS;
}

void MotorcycleTracer::propagateAcrossEdge(const Motorcycle& mot, const HEH& he, const HFH& hfWall)
{
    const TetMesh& tetMesh = meshProps().mesh();

    // Elements needed for circulating around he
    HEH heOpp = tetMesh.opposite_halfedge_handle(he);
    HFH hfStart = tetMesh.adjacent_halfface_in_cell(hfWall, he);
    HFH hfStop = tetMesh.opposite_halfface_handle(
        tetMesh.adjacent_halfface_in_cell(tetMesh.opposite_halfface_handle(hfWall), heOpp));

    assert(!tetMesh.is_boundary(he));
    assert(!meshProps().get<IS_SINGULAR>(tetMesh.edge_handle(he)));
    bool hasPropagated = false;
    int passed = 0;
    Transition totalTransition;
    Vec3i coords = mot.encodedCoords;
    Vec3Q values(0, 0, 0);
    values[mot.isoCoord()] = mot.isoValue;
    values[mot.propagationCoord()] = mot.startValue;

    bool insideOriginalTet = true;
    TetMeshNavigator::forEachHfInHeCycle(
        heOpp,
        hfStart,
        hfStop,
        [&hasPropagated, &passed, &insideOriginalTet, &totalTransition, &coords, &values, &mot, &heOpp, this](
            const HFH& hf)
        {
            if (meshProps().isAllocated<IS_ORIGINAL_F>()
                && meshProps().get<IS_ORIGINAL_F>(meshProps().mesh().face_handle(hf)))
                insideOriginalTet = false;
            totalTransition = totalTransition.chain(meshProps().hfTransition<TRANSITION>(hf));
            Vec3i currentCoords = totalTransition.rotate(coords);
            Vec3Q currentValues = totalTransition.apply(values);

            CH nextCell = meshProps().mesh().incident_cell(meshProps().mesh().opposite_halfface_handle(hf));
            assert(nextCell.is_valid());
            Motorcycle motNew(nextCell,
                              meshProps().mesh().edge_handle(heOpp),
                              currentCoords,
                              currentValues[Motorcycle::isoCoord(currentCoords)],
                              currentValues[Motorcycle::propagationCoord(currentCoords)],
                              0,
                              0);
            if (TetMeshNavigator::orientationRelativeToTet(motNew) != Orientation::OUTSIDE)
            {
                hasPropagated = true;
                motNew.directDist = getDirectDistToOrigin(motNew);
                motNew.dist = mot.dist + ::abs(motNew.directDist - mot.directDist);
                // When inside original tet, mutex-like behaviour
                // by inserting into local queue, which gets fully exhausted before next global queue mot is processed
                if (insideOriginalTet)
                    _localQ.push(motNew);
                else
                    _mQ.push(motNew);
                return true; // break afterwards
            }
            passed++;
            return false; // dont break afterwards
        });

    assert(hasPropagated);
}

bool MotorcycleTracer::isAlive(const EH& e) const
{
    if (meshProps().mesh().is_boundary(e))
        return false;

    if (meshProps().get<IS_SINGULAR>(e))
        return false;

    // if simulating BC (and not splitting some toroidal/selfadjacent blocks after initial complex build)
    // any non-boundary non-singular edge is alive
    if (_simulateBC && !meshProps().isAllocated<MC_BLOCK_DATA>())
        return true;

    int wallFacesIncidentOnNewEdge = 0;
    for (FH f : meshProps().mesh().edge_faces(e))
        if (meshProps().get<IS_WALL>(f))
            wallFacesIncidentOnNewEdge++;

    // We check this before pushing followup walls to the queue
    // Therefor only the first wall to reach an edge may propagate a followup wall through that edge
    return wallFacesIncidentOnNewEdge < 2;
}

void MotorcycleTracer::clearNewWalls()
{
    newWalls.clear();
}

vector<FH> MotorcycleTracer::getNewWalls()
{
    for (auto itFace = newWalls.begin(); itFace != newWalls.end();)
    {
        if (meshProps().mesh().is_deleted(*itFace))
        {
            assert(meshProps().isAllocated<CHILD_FACES>());
            const auto& children = meshProps().get<CHILD_FACES>(*itFace);
            for (FH child : children)
                newWalls.push_back(child);
            itFace = newWalls.erase(itFace);
        }
        else
        {
            itFace++;
        }
    }

    return vector<FH>(newWalls.begin(), newWalls.end());
}

} // namespace mc3d
