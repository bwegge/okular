/***************************************************************************
 *   Copyright (C) 2012 by Mailson Menezes <mailson@gmail.com>             *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "tilesmanager_p.h"

#include <QPixmap>
#include <QtCore/qmath.h>
#include <QList>

#ifdef TILES_DEBUG
#include <QPainter>
#endif

#define RANGE_MAX 1073741823
#define RANGE_MIN -1073741824

using namespace Okular;

static bool rankedTilesLessThan( Tile *t1, Tile *t2 )
{
    if ( t1->dirty ^ t2->dirty )
        return t1->miss < t2->miss;

    return !t1->dirty;
}

class TilesManager::Private
{
    public:
        Private();

        bool hasPixmap( const NormalizedRect &rect, const Tile &tile ) const;
        void tilesAt( const NormalizedRect &rect, Tile &tile, QList<Tile> &result, bool allowEmpty );
        void setPixmap( const QPixmap *pixmap, const NormalizedRect &rect, Tile &tile );

        /**
         * Mark @p tile and all its children as dirty
         */
        void markDirty( Tile &tile );

        /**
         * Deletes all tiles, recursively
         */
        void deleteTiles( const Tile &tile );

        void onClearPixmap( const Tile &tile );
        void rankTiles( Tile &tile );
        /**
         * Since the tile can be large enough to occupy a significant amount of
         * space, they may be split in more tiles. This operation is performed
         * when the tiles of a certain region is requested and they are bigger
         * than an arbitrary value. Only tiles intersecting the desired region
         * are split. There's no need to do this for the entire page.
         */
        void split( Tile &tile, const NormalizedRect &rect );

        Tile tiles[16];
        int width;
        int height;
        long totalPixels;
        Rotation rotation;

        QList<Tile*> rankedTiles;
        QSize tileSize;
};

TilesManager::Private::Private()
    : width( 0 )
    , height( 0 )
    , totalPixels( 0 )
    , rotation( Rotation0 )
    , tileSize( QSize() )
{
}

TilesManager::TilesManager( int width, int height, Rotation rotation )
    : d( new Private )
{
    d->width = width;
    d->height = height;
    d->rotation = rotation;

    const double dim = 0.25;
    for ( int i = 0; i < 16; ++i )
    {
        int x = i % 4;
        int y = i / 4;
        d->tiles[ i ].rect = NormalizedRect( x*dim, y*dim, x*dim+dim, y*dim+dim );
    }
}

TilesManager::~TilesManager()
{
    for ( int i = 0; i < 16; ++i )
        d->deleteTiles( d->tiles[ i ] );
}

void TilesManager::Private::deleteTiles( const Tile &tile )
{
    if ( tile.pixmap )
        delete tile.pixmap;

    for ( int i = 0; i < tile.nTiles; ++i )
        deleteTiles( tile.tiles[ i ] );
}

void TilesManager::setWidth( int width )
{
    if ( width == d->width )
        return;

    d->width = width;

    for ( int i = 0; i < 16; ++i )
    {
        d->markDirty( d->tiles[ i ] );
    }
}

int TilesManager::width() const {
    return d->width;
}

void TilesManager::setHeight( int height )
{
    if ( height == d->height )
        return;

    d->height = height;
}

int TilesManager::height() const {
    return d->height;
}

void TilesManager::setRotation( Rotation rotation )
{
    if ( rotation == d->rotation )
        return;

    d->rotation = rotation;

    for ( int i = 0; i < 16; ++i )
    {
        d->markDirty( d->tiles[ i ] );
    }
}

Rotation TilesManager::rotation() const
{
    return d->rotation;
}

void TilesManager::Private::markDirty( Tile &tile )
{
    tile.dirty = true;

    for ( int i = 0; i < tile.nTiles; ++i )
    {
        markDirty( tile.tiles[ i ] );
    }
}

void TilesManager::setPixmap( const QPixmap *pixmap, const NormalizedRect &rect )
{
    for ( int i = 0; i < 16; ++i )
    {
        d->setPixmap( pixmap, fromRotatedRect( rect, d->rotation ), d->tiles[ i ] );
    }
}

void TilesManager::Private::setPixmap( const QPixmap *pixmap, const NormalizedRect &rect, Tile &tile )
{
    QRect pixmapRect = TilesManager::toRotatedRect( rect, rotation ).geometry( width, height );

    if ( !tile.rect.intersects( rect ) )
        return;

    // the tile intersects an edge of the viewport
    if ( !((tile.rect & rect) == tile.rect) )
    {
        // paint children tiles
        if ( tile.nTiles > 0 )
        {
            for ( int i = 0; i < tile.nTiles; ++i )
                setPixmap( pixmap, rect, tile.tiles[ i ] );

            delete tile.pixmap;
            tile.pixmap = 0;
        }

        return;
    }

    if ( tile.nTiles == 0 )
    {
        tile.dirty = false;

        QRect tileRect = tile.rect.geometry( width, height );

        if ( tileRect.width()*tileRect.height() < TILES_MAXSIZE ) // size ok
        {
            if ( tile.pixmap )
            {
                totalPixels -= tile.pixmap->width()*tile.pixmap->height();
                delete tile.pixmap;
            }
            NormalizedRect rotatedRect = TilesManager::toRotatedRect( tile.rect, rotation );
            tile.pixmap = new QPixmap( pixmap->copy( rotatedRect.geometry( width, height ).translated( -pixmapRect.topLeft() ) ) );
            totalPixels += tile.pixmap->width()*tile.pixmap->height();

#ifdef TILES_DEBUG
            QRect pixRect = tile.pixmap->rect();
            QPainter p(tile.pixmap);
            p.drawLine(0,0,pixRect.right(),0);
            p.drawLine(pixRect.right(), 0, pixRect.right(), pixRect.bottom());
            p.drawLine(pixRect.right(), pixRect.bottom(), pixRect.left(), pixRect.bottom());
            p.drawLine(pixRect.left(), pixRect.bottom(), 0,0);
#endif // TILES_DEBUG
        }
        else
        {
            split( tile, rect );
            if ( tile.nTiles > 0 )
            {
                for ( int i = 0; i < tile.nTiles; ++i )
                    setPixmap( pixmap, rect, tile.tiles[ i ] );

                if ( tile.pixmap )
                {
                    totalPixels -= tile.pixmap->width()*tile.pixmap->height();
                    delete tile.pixmap;
                    tile.pixmap = 0;
                }
            }
        }
    }
    else
    {
        QRect tileRect = tile.rect.geometry( width, height );
        if ( tileRect.width()*tileRect.height() >= TILES_MAXSIZE ) // size ok
        {
            tile.dirty = false;
            for ( int i = 0; i < tile.nTiles; ++i )
                setPixmap( pixmap, rect, tile.tiles[ i ] );

            if ( tile.pixmap )
            {
                totalPixels -= tile.pixmap->width()*tile.pixmap->height();
                delete tile.pixmap;
                tile.pixmap = 0;
            }
        }
        else // size not ok (too small)
        {
            // remove children tiles
            tile.rect = NormalizedRect();
            for ( int i = 0; i < tile.nTiles; ++i )
            {
                if ( tile.rect.isNull() )
                    tile.rect = tile.tiles[ i ].rect;
                else
                    tile.rect |= tile.tiles[ i ].rect;

                if ( tile.tiles[ i ].pixmap )
                {
                    totalPixels -= tile.tiles[ i ].pixmap->width()*tile.tiles[ i ].pixmap->height();
                    delete tile.tiles[ i ].pixmap;
                }
                tile.tiles[ i ].pixmap = 0;
            }

            tileSize = tile.rect.geometry( width, height ).size();

            delete [] tile.tiles;
            tile.tiles = 0;
            tile.nTiles = 0;

            // paint tile
            if ( tile.pixmap )
            {
                totalPixels -= tile.pixmap->width()*tile.pixmap->height();
                delete tile.pixmap;
            }
            tile.pixmap = new QPixmap( pixmap->copy( tile.rect.geometry( width, height ).translated( -pixmapRect.topLeft() ) ) );
            totalPixels += tile.pixmap->width()*tile.pixmap->height();
            tile.dirty = false;
        }
    }
}

bool TilesManager::hasPixmap( const NormalizedRect &rect )
{
    for ( int i = 0; i < 16; ++i )
    {
        if ( !d->hasPixmap( fromRotatedRect( rect, d->rotation ), d->tiles[ i ] ) )
            return false;
    }

    return true;
}

bool TilesManager::Private::hasPixmap( const NormalizedRect &rect, const Tile &tile ) const
{
    if ( !tile.rect.intersects( rect ) )
        return true;

    if ( tile.nTiles == 0 )
        return tile.pixmap && !tile.dirty;

    // all children tiles are clean. doesn't need to go deeper
    if ( !tile.dirty )
        return true;

    for ( int i = 0; i < tile.nTiles; ++i )
    {
        if ( !hasPixmap( rect, tile.tiles[ i ] ) )
            return false;
    }

    return true;
}

QList<Tile> TilesManager::tilesAt( const NormalizedRect &rect, bool allowEmpty )
{
    QList<Tile> result;

    for ( int i = 0; i < 16; ++i )
    {
        d->tilesAt( fromRotatedRect( rect, d->rotation ), d->tiles[ i ], result, allowEmpty );
    }

    return result;
}

void TilesManager::Private::tilesAt( const NormalizedRect &rect, Tile &tile, QList<Tile> &result, bool allowEmpty )
{
    if ( !tile.rect.intersects( rect ) )
    {
        tile.miss = qMin( tile.miss+1, RANGE_MAX );
        return;
    }

    // split tile (if necessary)
    split( tile, rect );

    if ( ( allowEmpty && tile.nTiles == 0 ) || ( !allowEmpty && tile.pixmap ) )
    {
        // TODO: check tile size
        tile.miss = qMax( tile.miss-1, RANGE_MIN );
        Tile newTile = tile;
        if ( rotation != Rotation0 )
            newTile.rect = TilesManager::toRotatedRect( tile.rect, rotation );
        result.append( newTile );
    }
    else
    {
        for ( int i = 0; i < tile.nTiles; ++i )
            tilesAt( rect, tile.tiles[ i ], result, allowEmpty );
    }
}

long TilesManager::totalMemory() const
{
    return 4*d->totalPixels;
}

void TilesManager::cleanupPixmapMemory( qulonglong numberOfBytes )
{
    d->rankedTiles.clear();
    for ( int i = 0; i < 16; ++i )
    {
        d->rankTiles( d->tiles[ i ] );
    }
    qSort( d->rankedTiles.begin(), d->rankedTiles.end(), rankedTilesLessThan );

    while ( numberOfBytes > 0 && !d->rankedTiles.isEmpty() )
    {
        Tile *tile = d->rankedTiles.takeLast();
        if ( !tile->pixmap )
            continue;

        long pixels = tile->pixmap->width()*tile->pixmap->height();
        d->totalPixels -= pixels;
        if ( numberOfBytes < 4*pixels )
            numberOfBytes = 0;
        else
            numberOfBytes -= 4*pixels;

        tile->miss = 0;
        delete tile->pixmap;
        tile->pixmap = 0;

        d->onClearPixmap( *tile );
    }
}

void TilesManager::Private::onClearPixmap( const Tile &tile )
{
    if ( !tile.parent )
        return;

    if ( !tile.parent->dirty )
    {
        tile.parent->dirty = true;
        onClearPixmap( *tile.parent );
    }
}

void TilesManager::Private::rankTiles( Tile &tile )
{
    if ( tile.parent )
        tile.miss = qBound( RANGE_MIN, tile.miss + tile.parent->miss, RANGE_MAX );

    if ( tile.pixmap )
    {
        rankedTiles.append( &tile );
    }
    else
    {
        for ( int i = 0; i < tile.nTiles; ++i )
        {
            rankTiles( tile.tiles[ i ] );
        }

        if ( tile.nTiles > 0 )
            tile.miss = 0;
    }
}

void TilesManager::Private::split( Tile &tile, const NormalizedRect &rect )
{
    if ( tile.nTiles != 0 )
        return;

    if ( rect.isNull() || !tile.rect.intersects( rect ) )
        return;

    QRect tileRect = tile.rect.geometry( width, height );
    if ( tileRect.width()*tileRect.height() >= TILES_MAXSIZE )
    {
        tile.nTiles = 4;
        tile.tiles = new Tile[4];
        double hCenter = (tile.rect.left + tile.rect.right)/2;
        double vCenter = (tile.rect.top + tile.rect.bottom)/2;

        tile.tiles[0].rect = NormalizedRect( tile.rect.left, tile.rect.top, hCenter, vCenter );
        tile.tiles[1].rect = NormalizedRect( hCenter, tile.rect.top, tile.rect.right, vCenter );
        tile.tiles[2].rect = NormalizedRect( tile.rect.left, vCenter, hCenter, tile.rect.bottom );
        tile.tiles[3].rect = NormalizedRect( hCenter, vCenter, tile.rect.right, tile.rect.bottom );

        tileSize = tile.tiles[0].rect.geometry( width, height ).size();

        for ( int i = 0; i < tile.nTiles; ++i )
        {
            tile.tiles[ i ].parent = &tile;
            split( tile.tiles[ i ], rect );
        }
    }
}

NormalizedRect TilesManager::fromRotatedRect( const NormalizedRect &rect, Rotation rotation )
{
    if ( rotation == Rotation0 )
        return rect;

    NormalizedRect newRect;
    switch ( rotation )
    {
        case Rotation90:
            newRect = NormalizedRect( rect.top, 1 - rect.right, rect.bottom, 1 - rect.left );
            break;
        case Rotation180:
            newRect = NormalizedRect( 1 - rect.right, 1 - rect.bottom, 1 - rect.left, 1 - rect.top );
            break;
        case Rotation270:
            newRect = NormalizedRect( 1 - rect.bottom, rect.left, 1 - rect.top, rect.right );
            break;
        default:
            newRect = rect;
            break;
    }

    return newRect;
}

NormalizedRect TilesManager::toRotatedRect( const NormalizedRect &rect, Rotation rotation )
{
    if ( rotation == Rotation0 )
        return rect;

    NormalizedRect newRect;
    switch ( rotation )
    {
        case Rotation90:
            newRect = NormalizedRect( 1 - rect.bottom, rect.left, 1 - rect.top, rect.right );
            break;
        case Rotation180:
            newRect = NormalizedRect( 1 - rect.right, 1 - rect.bottom, 1 - rect.left, 1 - rect.top );
            break;
        case Rotation270:
            newRect = NormalizedRect( rect.top, 1 - rect.right, rect.bottom, 1 - rect.left );
            break;
        default:
            newRect = rect;
            break;
    }

    return newRect;
}

Tile::Tile()
    : pixmap( 0 )
    , dirty ( true )
    , tiles( 0 )
    , nTiles( 0 )
    , parent( 0 )
    , miss( 0 )
{
}

bool Tile::isValid() const
{
    return pixmap && !dirty;
}