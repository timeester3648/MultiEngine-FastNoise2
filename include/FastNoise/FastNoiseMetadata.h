#pragma once
#include <functional>
#include <memory>
#include <type_traits>

#include "FastSIMD/FastSIMD.h"

namespace FastNoise
{
    class Generator;
    template<typename T>
    struct PerDimensionVariable;

    struct Metadata
    {
        Metadata() = delete;
        inline Metadata( const char* );

        struct MemberVariable
        {
            enum eType
            {
                EFloat,
                EInt,
                EEnum
            };

            union ValueUnion
            {
                float f;
                int32_t i;

                ValueUnion( float v = 0 )
                {
                    f = v;
                }

                ValueUnion( int32_t v )
                {
                    i = v;
                }

                operator float()
                {
                    return f;
                }

                operator int32_t()
                {
                    return i;
                }
            };

            const char* name;
            eType type;
            int dimensionIdx = -1;
            ValueUnion valueDefault, valueMin, valueMax;
            std::vector<const char*> enumNames;

            std::function<void(Generator*, ValueUnion)> setFunc;
        };

        //! Specialisation for Functor/Lambdas
        template<typename F, typename Ret, typename... Args>
        static std::tuple<Args...> getArgs( Ret( F::* )(Args...) const );

        template<typename F, std::size_t I>
        using GetArg = std::tuple_element_t<I, decltype(getArgs( &F::operator() ))>;

        template<typename T, typename U, typename = std::enable_if_t<!std::is_enum_v<T>>>
        void AddVariable( const char* name, T defaultV, U&& func, T minV = 0, T maxV = 0 )
        {
            MemberVariable member;
            member.name = name;
            member.valueDefault = defaultV;
            member.valueMin = minV;
            member.valueMax = maxV;

            member.type = std::is_same_v<T, float> ? MemberVariable::EFloat : MemberVariable::EInt;            

            member.setFunc = [func]( Generator* g, MemberVariable::ValueUnion v ) { func( dynamic_cast<GetArg<U, 0>>(g), v ); };

            memberVariables.push_back( member );
        }

        template<typename T, typename U, typename = std::enable_if_t<!std::is_enum_v<T>>>
        void AddVariable( const char* name, T defaultV, void( U::*func )( T ), T minV = 0, T maxV = 0 )
        {
            MemberVariable member;
            member.name = name;
            member.valueDefault = defaultV;
            member.valueMin = minV;
            member.valueMax = maxV;

            member.type = std::is_same_v<T, float> ? MemberVariable::EFloat : MemberVariable::EInt;

            member.setFunc = [func]( Generator* g, MemberVariable::ValueUnion v ) { ( dynamic_cast<U*>(g)->*func )( v ); };

            memberVariables.push_back( member );
        }

        template<typename T, typename U, typename = std::enable_if_t<std::is_enum_v<T>>, typename... NAMES>
        void AddVariableEnum( const char* name, T defaultV, void(U::* func)( T ), NAMES... names )
        {
            MemberVariable member;
            member.name = name;
            member.type = MemberVariable::EEnum;
            member.valueDefault = (int32_t)defaultV;
            member.enumNames = { names... };

            member.setFunc = [func]( Generator* g, MemberVariable::ValueUnion v ) { (dynamic_cast<U*>(g)->*func)( (T)v.i ); };

            memberVariables.push_back( member );
        }

        template<typename T, typename U, typename = std::enable_if_t<!std::is_enum_v<T>>>
        void AddPerDimensionVariable( const char* name, T defaultV, U&& func, T minV = 0, T maxV = 0 )
        {
            for( int idx = 0; (size_t)idx < sizeof( PerDimensionVariable<T>::varArray ) / sizeof( *PerDimensionVariable<T>::varArray ); idx++ )
            {
                MemberVariable member;
                member.name = name;
                member.valueDefault = defaultV;
                member.valueMin = minV;
                member.valueMax = maxV;

                member.type = std::is_same_v<T, float> ? MemberVariable::EFloat : MemberVariable::EInt;
                member.dimensionIdx = idx;

                member.setFunc = [func, idx]( Generator* g, MemberVariable::ValueUnion v ) { func( dynamic_cast<GetArg<U, 0>>(g) ).get()[idx] = v; };

                memberVariables.push_back( member );
            }
        }

        struct MemberNode
        {
            const char* name;
            int dimensionIdx = -1;

            std::function<bool(Generator*, const std::shared_ptr<Generator>&)> setFunc;
        };

        template<typename T, typename U>
        void AddGeneratorSource( const char* name, void(U::* func)(const std::shared_ptr<T>&) )
        {
            MemberNode member;
            member.name = name;

            member.setFunc = [func]( Generator* g, const std::shared_ptr<Generator>& s )
            {
                std::shared_ptr<T> downCast = std::dynamic_pointer_cast<T>( s );
                if( downCast )
                {
                    (dynamic_cast<U*>(g)->*func)( downCast );
                }
                return (bool)downCast;
            };

            memberNodes.push_back( member );
        }

        template<typename U>
        void AddPerDimensionGeneratorSource( const char* name, U&& func )
        {
            using GeneratorSourceT = typename std::invoke_result_t<U, GetArg<U, 0>>::type::Type;
            using T = typename GeneratorSourceT::Type;

            for( int idx = 0; (size_t)idx < sizeof( PerDimensionVariable<GeneratorSourceT>::varArray ) / sizeof( *PerDimensionVariable<GeneratorSourceT>::varArray ); idx++ )
            {
                MemberNode member;
                member.name = name;
                member.dimensionIdx = idx;

                member.setFunc = [func, idx]( Generator* g, const std::shared_ptr<Generator>& s )
                {
                    std::shared_ptr<T> downCast = std::dynamic_pointer_cast<T>(s);
                    if( downCast )
                    {
                        g->SetSourceMemberVariable( func( dynamic_cast<GetArg<U, 0>>(g) ).get()[idx], downCast );
                    }
                    return (bool)downCast;
                };

                memberNodes.push_back( member );
            }
        }

        struct MemberHybrid
        {
            const char* name;
            float valueDefault = 0.0f;
            int dimensionIdx = -1;

            std::function<void( Generator*, float )> setValueFunc;
            std::function<bool( Generator*, const std::shared_ptr<Generator>& )> setNodeFunc;
        };

        template<typename T, typename U>
        void AddHybridSource( const char* name, float defaultValue, void(U::* funcNode)(const std::shared_ptr<T>&), void(U::* funcValue)(float) )
        {
            MemberHybrid member;
            member.name = name;
            member.valueDefault = defaultValue;

            member.setNodeFunc = [funcNode]( Generator* g, const std::shared_ptr<Generator>& s )
            {
                std::shared_ptr<T> downCast = std::dynamic_pointer_cast<T>( s );
                if( downCast )
                {
                    (dynamic_cast<U*>(g)->*funcNode)( downCast );
                }
                return (bool)downCast;
            };

            member.setValueFunc = [funcValue]( Generator* g, float v )
            {
                (dynamic_cast<U*>(g)->*funcValue)( v );
            };

            memberHybrids.push_back( member );
        }

        template<typename U>
        void AddPerDimensionHybridSource( const char* name, float defaultV, U&& func )
        {
            using HybridSourceT = typename std::invoke_result_t<U, GetArg<U, 0>>::type::Type;
            using T             = typename HybridSourceT::Type;

            for( int idx = 0; (size_t)idx < sizeof( PerDimensionVariable<HybridSourceT>::varArray ) / sizeof( *PerDimensionVariable<HybridSourceT>::varArray ); idx++ )
            {
                MemberHybrid member;
                member.name = name;
                member.valueDefault = defaultV;
                member.dimensionIdx = idx;

                member.setNodeFunc = [func, idx]( Generator* g, const std::shared_ptr<Generator>& s )
                {
                    std::shared_ptr<T> downCast = std::dynamic_pointer_cast<T>( s );
                    if( downCast )
                    {
                        g->SetSourceMemberVariable( func( dynamic_cast<GetArg<U, 0>>(g) ).get()[idx], downCast );
                    }
                    return (bool)downCast;
                };

                member.setValueFunc = [func, idx]( Generator* g, float v ) { func( dynamic_cast<GetArg<U, 0>>(g) ).get()[idx] = v; };

                memberHybrids.push_back( member );
            }
        }

        const char* name;
        uint16_t id;

        std::vector<MemberVariable> memberVariables;
        std::vector<MemberNode>     memberNodes;
        std::vector<MemberHybrid>   memberHybrids;

        virtual Generator* NodeFactory( FastSIMD::eLevel level = FastSIMD::Level_Null ) const = 0;
    };

    struct NodeData
    {
        const Metadata* metadata = nullptr;
        std::vector<Metadata::MemberVariable::ValueUnion> variables;
        std::vector<const NodeData*> nodes;
        std::vector<std::pair<const NodeData*, float>> hybrids;
    };

    class MetadataManager
    {
    public:
        static uint16_t AddMetadataClass( Metadata* newMetadata )
        {
            sMetadataClasses.emplace_back( newMetadata );

            return (uint16_t)sMetadataClasses.size() - 1;
        }

        static const std::vector<const Metadata*>& GetMetadataClasses()
        {
            return sMetadataClasses;
        }

        static const Metadata* GetMetadataClass( uint16_t nodeId )
        {
            if( nodeId < sMetadataClasses.size() )
            {
                return sMetadataClasses[nodeId];
            }

            return nullptr;
        }

        static std::string SerialiseNodeData( const NodeData* nodeData );
        static bool SerialiseNodeData( const NodeData* nodeData, std::vector<uint8_t>& dataStream );

        static std::shared_ptr<Generator> DeserialiseNodeData( const char* serialisedBase64NodeData, FastSIMD::eLevel level = FastSIMD::Level_Null );
        static std::shared_ptr<Generator> DeserialiseNodeData( const std::vector<uint8_t>& serialisedNodeData, size_t& serialIdx, FastSIMD::eLevel level = FastSIMD::Level_Null );

    private:
        MetadataManager() = delete;

        static std::vector<const Metadata*> sMetadataClasses;
    };

    Metadata::Metadata( const char* className )
    {
        name = className;
        id = MetadataManager::AddMetadataClass( this );
    }

#define FASTNOISE_METADATA( ... ) public:\
    FASTSIMD_LEVEL_SUPPORT( FastNoise::SUPPORTED_SIMD_LEVELS );\
    const FastNoise::Metadata* GetMetadata() override;\
    struct Metadata : __VA_ARGS__::Metadata{\
    virtual Generator* NodeFactory( FastSIMD::eLevel ) const override;

#define FASTNOISE_METADATA_ABSTRACT( ... ) public:\
    struct Metadata : __VA_ARGS__::Metadata{
}
