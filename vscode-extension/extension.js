const vscode = require('vscode');
const fs = require('fs');
const path = require('path');

// =========================================================
// COI Definition Parser
// =========================================================

class CoiDefinitions {
    constructor() {
        this.types = new Map();      // type Name { ... }
        this.namespaces = new Map(); // namespace Name { ... }
        this.components = new Map(); // component Name { ... } from user files
    }

    // Parse all .d.coi files from a directory
    loadFromDirectory(defPath) {
        if (!fs.existsSync(defPath)) return;
        
        const files = fs.readdirSync(defPath).filter(f => f.endsWith('.d.coi'));
        for (const file of files) {
            const content = fs.readFileSync(path.join(defPath, file), 'utf8');
            this.parseDefinitionFile(content, file);
        }
    }

    // Parse a single .d.coi file
    parseDefinitionFile(content, filename) {
        const lines = content.split('\n');
        let currentBlock = null;  // { kind: 'type'|'namespace', name: string, methods: [] }
        let braceDepth = 0;

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            const trimmed = line.trim();

            // Skip comments and empty lines
            if (trimmed.startsWith('//') || trimmed === '') continue;

            // Match type or namespace declaration
            const typeMatch = trimmed.match(/^type\s+(\w+)(?:\s+extends\s+(\w+))?\s*\{/);
            const nsMatch = trimmed.match(/^namespace\s+(\w+)\s*\{/);

            if (typeMatch) {
                currentBlock = {
                    kind: 'type',
                    name: typeMatch[1],
                    extends: typeMatch[2] || null,
                    methods: [],
                    typeMethods: [],  // type def (static)
                    source: filename
                };
                braceDepth = 1;
                continue;
            }

            if (nsMatch) {
                currentBlock = {
                    kind: 'namespace',
                    name: nsMatch[1],
                    methods: [],
                    source: filename
                };
                braceDepth = 1;
                continue;
            }

            if (currentBlock) {
                // Track braces
                braceDepth += (line.match(/\{/g) || []).length;
                braceDepth -= (line.match(/\}/g) || []).length;

                // Parse method definitions
                // type def methodName(...): ReturnType { ... }
                const typeDefMatch = trimmed.match(/^type\s+def\s+(\w+)\s*\(([^)]*)\)\s*:\s*(\w+)/);
                // def methodName(...): ReturnType { ... }
                const defMatch = trimmed.match(/^def\s+(\w+)\s*\(([^)]*)\)\s*:\s*(\w+)/);
                // // maps to: ns::func
                const mapsToMatch = trimmed.match(/\/\/\s*maps to:\s*(\w+)::(\w+)/);

                if (typeDefMatch) {
                    const method = {
                        name: typeDefMatch[1],
                        params: this.parseParams(typeDefMatch[2]),
                        returnType: typeDefMatch[3],
                        isTypeMethod: true,
                        mapsTo: null
                    };
                    currentBlock.typeMethods.push(method);
                    currentBlock._lastMethod = method;
                } else if (defMatch) {
                    const method = {
                        name: defMatch[1],
                        params: this.parseParams(defMatch[2]),
                        returnType: defMatch[3],
                        isTypeMethod: false,
                        mapsTo: null
                    };
                    currentBlock.methods.push(method);
                    currentBlock._lastMethod = method;
                } else if (mapsToMatch && currentBlock._lastMethod) {
                    currentBlock._lastMethod.mapsTo = `${mapsToMatch[1]}::${mapsToMatch[2]}`;
                }

                // End of block
                if (braceDepth === 0) {
                    delete currentBlock._lastMethod;
                    if (currentBlock.kind === 'type') {
                        this.types.set(currentBlock.name, currentBlock);
                    } else {
                        this.namespaces.set(currentBlock.name, currentBlock);
                    }
                    currentBlock = null;
                }
            }
        }
    }

    // Parse parameter list: "x: int, y: float" -> [{name: 'x', type: 'int'}, ...]
    parseParams(paramStr) {
        if (!paramStr.trim()) return [];
        return paramStr.split(',').map(p => {
            const [name, type] = p.split(':').map(s => s.trim());
            return { name, type };
        });
    }

    // Parse a user .coi file to extract components
    parseUserFile(content) {
        const components = new Map();
        const lines = content.split('\n');
        let currentComponent = null;
        let braceDepth = 0;

        for (const line of lines) {
            const trimmed = line.trim();
            
            // component Name {
            const compMatch = trimmed.match(/^component\s+(\w+)\s*\{/);
            if (compMatch) {
                currentComponent = {
                    name: compMatch[1],
                    props: [],
                    state: [],
                    methods: []
                };
                braceDepth = 1;
                continue;
            }

            if (currentComponent) {
                braceDepth += (line.match(/\{/g) || []).length;
                braceDepth -= (line.match(/\}/g) || []).length;

                // prop mut? type& name;
                const propMatch = trimmed.match(/^prop\s+(mut\s+)?(\w+)(&)?\s+(\w+)/);
                if (propMatch) {
                    currentComponent.props.push({
                        name: propMatch[4],
                        type: propMatch[2],
                        mutable: !!propMatch[1],
                        reference: !!propMatch[3]
                    });
                }

                // mut? type name = ...;
                const stateMatch = trimmed.match(/^(mut\s+)?(\w+)\s+(\w+)\s*=/);
                if (stateMatch && !trimmed.startsWith('prop')) {
                    currentComponent.state.push({
                        name: stateMatch[3],
                        type: stateMatch[2],
                        mutable: !!stateMatch[1]
                    });
                }

                // def name(...) : type {
                const methodMatch = trimmed.match(/^def\s+(\w+)\s*\(([^)]*)\)\s*:\s*(\w+)/);
                if (methodMatch) {
                    currentComponent.methods.push({
                        name: methodMatch[1],
                        params: this.parseParams(methodMatch[2]),
                        returnType: methodMatch[3]
                    });
                }

                if (braceDepth === 0) {
                    components.set(currentComponent.name, currentComponent);
                    currentComponent = null;
                }
            }
        }

        return components;
    }
}

// =========================================================
// VS Code Extension
// =========================================================

let definitions = new CoiDefinitions();

function activate(context) {
    console.log('COI Language extension activated');

    // Load definitions from bundled def/ folder or custom path
    loadDefinitions(context);

    // Register completion provider
    const completionProvider = vscode.languages.registerCompletionItemProvider(
        'coi',
        {
            provideCompletionItems(document, position) {
                return getCompletions(document, position);
            }
        },
        '.', // Trigger on dot
        '<'  // Trigger on < for JSX tags
    );

    // Register hover provider
    const hoverProvider = vscode.languages.registerHoverProvider('coi', {
        provideHover(document, position) {
            return getHover(document, position);
        }
    });

    // Register signature help
    const signatureProvider = vscode.languages.registerSignatureHelpProvider(
        'coi',
        {
            provideSignatureHelp(document, position) {
                return getSignatureHelp(document, position);
            }
        },
        '(', ','
    );

    context.subscriptions.push(completionProvider, hoverProvider, signatureProvider);
}

function loadDefinitions(context) {
    // Check for custom definitions path
    const config = vscode.workspace.getConfiguration('coi');
    let defPath = config.get('definitionsPath');

    if (!defPath) {
        // Use bundled definitions
        defPath = path.join(context.extensionPath, 'def');
    }

    // Also check workspace root for def/ folder
    if (vscode.workspace.workspaceFolders) {
        const workspaceDefPath = path.join(vscode.workspace.workspaceFolders[0].uri.fsPath, 'def');
        if (fs.existsSync(workspaceDefPath)) {
            defPath = workspaceDefPath;
        }
    }

    definitions = new CoiDefinitions();
    definitions.loadFromDirectory(defPath);
    console.log(`Loaded ${definitions.types.size} types, ${definitions.namespaces.size} namespaces`);
}

function getCompletions(document, position) {
    const items = [];
    const lineText = document.lineAt(position).text;
    const textBefore = lineText.substring(0, position.character);

    // Check if we're after a dot (method completion)
    const dotMatch = textBefore.match(/(\w+)\.\s*(\w*)$/);
    if (dotMatch) {
        const typeName = dotMatch[1];
        const partial = dotMatch[2] || '';

        // Check if it's a known type (for type methods like Canvas.createCanvas)
        const typeInfo = definitions.types.get(typeName);
        if (typeInfo) {
            // Add type methods (static)
            for (const method of typeInfo.typeMethods || []) {
                if (method.name.toLowerCase().startsWith(partial.toLowerCase())) {
                    const item = new vscode.CompletionItem(method.name, vscode.CompletionItemKind.Method);
                    item.detail = `${typeName}.${method.name}(${formatParams(method.params)}): ${method.returnType}`;
                    item.documentation = method.mapsTo ? `Maps to: ${method.mapsTo}` : '';
                    item.insertText = new vscode.SnippetString(
                        method.name + '(' + createSnippetParams(method.params) + ')'
                    );
                    items.push(item);
                }
            }
        }

        // Check if it's a namespace
        const nsInfo = definitions.namespaces.get(typeName);
        if (nsInfo) {
            for (const method of nsInfo.methods) {
                if (method.name.toLowerCase().startsWith(partial.toLowerCase())) {
                    const item = new vscode.CompletionItem(method.name, vscode.CompletionItemKind.Function);
                    item.detail = `${typeName}.${method.name}(${formatParams(method.params)}): ${method.returnType}`;
                    item.documentation = method.mapsTo ? `Maps to: ${method.mapsTo}` : '';
                    item.insertText = new vscode.SnippetString(
                        method.name + '(' + createSnippetParams(method.params) + ')'
                    );
                    items.push(item);
                }
            }
        }

        // Check if it's a variable - find its type and show instance methods
        const varType = findVariableType(document, position, typeName);
        if (varType) {
            const typeInfo = definitions.types.get(varType);
            if (typeInfo) {
                for (const method of typeInfo.methods) {
                    if (method.name.toLowerCase().startsWith(partial.toLowerCase())) {
                        const item = new vscode.CompletionItem(method.name, vscode.CompletionItemKind.Method);
                        item.detail = `${method.name}(${formatParams(method.params)}): ${method.returnType}`;
                        item.documentation = method.mapsTo ? `Maps to: ${method.mapsTo}` : '';
                        item.insertText = new vscode.SnippetString(
                            method.name + '(' + createSnippetParams(method.params) + ')'
                        );
                        items.push(item);
                    }
                }
            }
        }

        return items;
    }

    // Check for JSX tag completion
    if (textBefore.match(/<(\w*)$/)) {
        // Parse current document for components
        const userComponents = definitions.parseUserFile(document.getText());
        
        // Add user-defined components
        for (const [name, comp] of userComponents) {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Class);
            item.detail = `component ${name}`;
            const propDesc = comp.props.map(p => `${p.name}: ${p.type}`).join(', ');
            item.documentation = propDesc ? `Props: ${propDesc}` : 'No props';
            items.push(item);
        }

        // Add HTML tags
        const htmlTags = ['div', 'span', 'p', 'h1', 'h2', 'h3', 'button', 'input', 'a', 'img', 'ul', 'li', 'canvas'];
        for (const tag of htmlTags) {
            const item = new vscode.CompletionItem(tag, vscode.CompletionItemKind.Property);
            item.detail = `HTML <${tag}>`;
            items.push(item);
        }

        return items;
    }

    // Top-level completions (types, namespaces, keywords)
    // Keywords
    const keywords = ['component', 'def', 'view', 'style', 'prop', 'mut', 'tick', 'if', 'else', 'for', 'while', 'return', 'import', 'app', 'struct'];
    for (const kw of keywords) {
        const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
        items.push(item);
    }

    // Types
    const types = ['int', 'float', 'string', 'bool', 'void'];
    for (const t of types) {
        const item = new vscode.CompletionItem(t, vscode.CompletionItemKind.TypeParameter);
        items.push(item);
    }

    // Handle types from definitions
    for (const [name, info] of definitions.types) {
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Class);
        item.detail = info.extends ? `type ${name} extends ${info.extends}` : `type ${name}`;
        items.push(item);
    }

    // Namespaces from definitions
    for (const [name, info] of definitions.namespaces) {
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Module);
        item.detail = `namespace ${name}`;
        items.push(item);
    }

    return items;
}

function getHover(document, position) {
    const wordRange = document.getWordRangeAtPosition(position);
    if (!wordRange) return null;

    const word = document.getText(wordRange);
    const lineText = document.lineAt(position).text;

    // Check if hovering over Type.method
    const dotPattern = new RegExp(`(\\w+)\\.${word}\\b`);
    const dotMatch = lineText.match(dotPattern);
    
    if (dotMatch) {
        const typeName = dotMatch[1];
        
        // Check type methods
        const typeInfo = definitions.types.get(typeName);
        if (typeInfo) {
            // Check type methods (static)
            const typeMethod = (typeInfo.typeMethods || []).find(m => m.name === word);
            if (typeMethod) {
                return new vscode.Hover([
                    `**${typeName}.${word}** (type method)`,
                    `\`\`\`coi\ntype def ${word}(${formatParams(typeMethod.params)}): ${typeMethod.returnType}\n\`\`\``,
                    typeMethod.mapsTo ? `Maps to: \`${typeMethod.mapsTo}\`` : ''
                ].filter(Boolean));
            }
            // Check instance methods
            const method = typeInfo.methods.find(m => m.name === word);
            if (method) {
                return new vscode.Hover([
                    `**${typeName}.${word}** (instance method)`,
                    `\`\`\`coi\ndef ${word}(${formatParams(method.params)}): ${method.returnType}\n\`\`\``,
                    method.mapsTo ? `Maps to: \`${method.mapsTo}\`` : ''
                ].filter(Boolean));
            }
        }

        // Check namespace
        const nsInfo = definitions.namespaces.get(typeName);
        if (nsInfo) {
            const method = nsInfo.methods.find(m => m.name === word);
            if (method) {
                return new vscode.Hover([
                    `**${typeName}.${word}**`,
                    `\`\`\`coi\ndef ${word}(${formatParams(method.params)}): ${method.returnType}\n\`\`\``,
                    method.mapsTo ? `Maps to: \`${method.mapsTo}\`` : ''
                ].filter(Boolean));
            }
        }
    }

    // Check if hovering over a type name
    const typeInfo = definitions.types.get(word);
    if (typeInfo) {
        const methodCount = (typeInfo.typeMethods || []).length + typeInfo.methods.length;
        return new vscode.Hover([
            `**type ${word}**` + (typeInfo.extends ? ` extends ${typeInfo.extends}` : ''),
            `${methodCount} methods`,
            `Source: ${typeInfo.source}`
        ]);
    }

    // Check if hovering over a namespace
    const nsInfo = definitions.namespaces.get(word);
    if (nsInfo) {
        return new vscode.Hover([
            `**namespace ${word}**`,
            `${nsInfo.methods.length} functions`,
            `Source: ${nsInfo.source}`
        ]);
    }

    return null;
}

function getSignatureHelp(document, position) {
    const lineText = document.lineAt(position).text.substring(0, position.character);
    
    // Find function call: TypeOrVar.method( or just method(
    const callMatch = lineText.match(/(\w+)\.(\w+)\s*\([^)]*$/);
    if (!callMatch) return null;

    const typeName = callMatch[1];
    const methodName = callMatch[2];

    let method = null;
    let container = typeName;

    // Search in types
    const typeInfo = definitions.types.get(typeName);
    if (typeInfo) {
        method = (typeInfo.typeMethods || []).find(m => m.name === methodName) ||
                 typeInfo.methods.find(m => m.name === methodName);
    }

    // Search in namespaces
    if (!method) {
        const nsInfo = definitions.namespaces.get(typeName);
        if (nsInfo) {
            method = nsInfo.methods.find(m => m.name === methodName);
        }
    }

    // Search by variable type
    if (!method) {
        const varType = findVariableType(document, position, typeName);
        if (varType) {
            const typeInfo = definitions.types.get(varType);
            if (typeInfo) {
                method = typeInfo.methods.find(m => m.name === methodName);
                container = varType;
            }
        }
    }

    if (!method) return null;

    const sig = new vscode.SignatureInformation(
        `${methodName}(${formatParams(method.params)}): ${method.returnType}`
    );
    
    sig.parameters = method.params.map(p => 
        new vscode.ParameterInformation(`${p.name}: ${p.type}`)
    );

    const help = new vscode.SignatureHelp();
    help.signatures = [sig];
    help.activeSignature = 0;
    
    // Count commas to determine active parameter
    const afterParen = lineText.substring(lineText.lastIndexOf('(') + 1);
    help.activeParameter = (afterParen.match(/,/g) || []).length;

    return help;
}

// Helper: find variable type by looking at declarations
function findVariableType(document, position, varName) {
    const text = document.getText();
    
    // Pattern: Type varName = ... or Type varName;
    const patterns = [
        new RegExp(`(\\w+)\\s+${varName}\\s*=`),
        new RegExp(`(\\w+)\\s+${varName}\\s*;`),
        new RegExp(`mut\\s+(\\w+)\\s+${varName}\\s*=`)
    ];

    for (const pattern of patterns) {
        const match = text.match(pattern);
        if (match) {
            return match[1];
        }
    }

    return null;
}

// Helper: format params for display
function formatParams(params) {
    return params.map(p => `${p.name}: ${p.type}`).join(', ');
}

// Helper: create snippet placeholders
function createSnippetParams(params) {
    return params.map((p, i) => `\${${i + 1}:${p.name}}`).join(', ');
}

function deactivate() {}

module.exports = { activate, deactivate };
