/*
Language: netifd
Category: netifd
*/

/** @type LanguageFn */
export default function(hljs) {
    const TYPE_NAME = '([\'"]?)[A-Za-z0-9_]+\\1(?=\\s|$)';
    const IDENT = '([\'"]?)[A-Za-z0-9_-]+\\1(?=\\s|$)';
    return {
        name: 'Netifd config',
        aliases: ['netifd', 'network'],
        disableAutodetect: true,
        contains: [
            hljs.COMMENT('#', '$'),
            {
                begin: 'config', end: '$',
                keywords: ['config'],
                contains: [
                    {
                        begin: 'route6?|interface', end: '$',
                        keywords: ['route', 'route6', 'interface', 'device'],
                        contains: [
                            {
                                scope: 'string',
                                begin: TYPE_NAME
                            }
                        ]
                    }
                ]
            },
            {
                begin: 'option|list', end: '$',
                keywords: ['option', 'list'],
                contains: [
                    {
                        scope: 'number',
                        begin: '([\'"]?)[0-9./:a-f]+\\1(?=\\s|$)'
                    },
                    {
                        scope: 'string',
                        begin: IDENT
                    }
                ]
            }
        ]
    };
}
